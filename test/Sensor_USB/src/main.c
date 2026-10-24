/**
 * @file main.c
 * @brief Apslim Rev.A: single TIMER2, USB sensors and simple meal counting test.
 *
 * nRF Connect SDK v3.4.1 / raytac_mdbt50q_db_40/nrf52840.
 * All application logic is intentionally in this file for the pre-v0.1 test.
 * P0.05 stays LOW until a continuously held, debounced button qualifies for 3 s.
 * No application delay loop, k_sleep(), k_msleep(), or k_busy_wait() is used.
 * Synchronous I2C/ADC driver calls run in main context, never in the timer ISR.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usb_device.h>

/* Defines / constants */
#define APP_TIMER_TICK_MS                 10U
#define BUTTON_DEBOUNCE_MS                30U
#define POWER_HOLD_PRESS_MS             3000U
#define DEBUG_LED_1_TOGGLE_MS             500U
#define DEBUG_LED_2_TOGGLE_MS             100U
#define ACCEL_SAMPLE_PERIOD_MS            20U
#define FDC_SAMPLE_PERIOD_MS              50U
#define BATTERY_SAMPLE_PERIOD_MS         500U
/* Display throttling only: sensor and meal processing keep their own cadence. */
#define USB_OUTPUT_PERIOD_MS             100U
/* 0: readable sensor groups; 1: append detailed timing/error diagnostics. */
#ifndef USB_OUTPUT_DIAGNOSTICS
#define USB_OUTPUT_DIAGNOSTICS             0U
#endif
/* Gate flags help distinguish electrode response from pose/rearm conditions. */
#ifndef USB_OUTPUT_MEAL_GATES
#define USB_OUTPUT_MEAL_GATES              1U
#endif
#define ACCEL_RESET_TIMEOUT_MS           100U
#define ACCEL_SETTLE_MS                   80U
/* LIS2DH12 HR turn-on can take 7 / ODR = 140 ms at 50 Hz. */
#define ACCEL_DH_SETTLE_MS               160U
#define ACCEL_STALE_MS                   200U
#define FDC_CONVERSION_TIMEOUT_MS        100U
#define FDC_STALE_MS                     250U
#define BATTERY_STALE_MS                1500U
#define USB_TX_BUFFER_BYTES             4096U
#define USB_HEADER_BUFFER_BYTES         2048U
#define USB_ROW_BUFFER_BYTES             512U
#define USB_IRQ_BUDGET                      8U

#define BATTERY_DIVIDER_HIGH_OHM      1000000U
#define BATTERY_DIVIDER_LOW_OHM        499000U

/* Any-axis recent motion, not a yaw angle or a physical displacement estimate. */
#define MEAL_MOTION_TRIGGER_MG              100L
#define MEAL_MOTION_HOLD_MS               2000U
#define MEAL_MOTION_WINDOW_MS              200U
#define MEAL_MOTION_HISTORY_SAMPLES         10U
#define MEAL_MOTION_FILTER_SAMPLES           4U
#define MEAL_MOTION_MIN_SAMPLES              3U
/* Manual spoon-attached reference; not an automatic zero calibration. */
#define MEAL_FDC_BASE_PF_X10000          69000L /* 6.9 pF */
#define MEAL_CONTACT_ON_DELTA_X10000     50000L /* provisional +5.0 pF */
#define MEAL_CONTACT_OFF_DELTA_X10000    20000L /* provisional +2.0 pF */
#define MEAL_CONTACT_ON_PF_X10000 \
    (MEAL_FDC_BASE_PF_X10000 + MEAL_CONTACT_ON_DELTA_X10000)
#define MEAL_CONTACT_OFF_PF_X10000 \
    (MEAL_FDC_BASE_PF_X10000 + MEAL_CONTACT_OFF_DELTA_X10000)
#define MEAL_CONTACT_CONFIRM_MS            100U
#define MEAL_RELEASE_CONFIRM_MS            100U
#define MEAL_REARM_CONFIRM_MS              300U

/* ST 8-bit READ address 0x33 = (7-bit address 0x19 << 1) | READ. */
#define ACCEL_I2C_READ_ADDRESS_8BIT       0x33U
#define ACCEL_ADDRESS_FIRST              (ACCEL_I2C_READ_ADDRESS_8BIT >> 1U)
#define ACCEL_ADDRESS_SECOND             0x18U
#define ACCEL_WHO_AM_I_REG               0x0FU
#define ACCEL_ID_LIS2DH12                0x33U
#define ACCEL_ID_LIS2DW12_FAMILY          0x44U
#define ACCEL_CTRL1_REG                  0x20U
#define ACCEL_CTRL2_REG                  0x21U
#define ACCEL_CTRL3_REG                  0x22U
#define ACCEL_CTRL4_REG                  0x23U
#define ACCEL_CTRL5_REG                  0x24U
#define ACCEL_CTRL6_REG                  0x25U
#define ACCEL_STATUS_REG                 0x27U
#define ACCEL_OUTPUT_REG                 0x28U
#define ACCEL_FIFO_CTRL_REG              0x2EU
#define ACCEL_SOFT_RESET                 0x40U
#define ACCEL_BDU_AUTO_INCREMENT         0x0CU
#define ACCEL_50HZ_HIGH_PERFORMANCE      0x44U
#define ACCEL_2G_ODR_DIV2                0x00U
#define ACCEL_DATA_READY_MASK            0x01U
#define ACCEL_DH_50HZ_XYZ                0x47U
#define ACCEL_DH_BDU_HR_2G               0x88U
#define ACCEL_DH_DATA_READY_MASK         0x08U
#define ACCEL_DH_AUTO_INCREMENT          0x80U

#define FDC_ADDRESS                     0x50U
#define FDC_MEAS1_MSB_REG                0x00U
#define FDC_MEAS1_LSB_REG                0x01U
#define FDC_MEAS1_CONFIG_REG             0x08U
#define FDC_CONFIG_REG                   0x0CU
#define FDC_MANUFACTURER_ID_REG          0xFEU
#define FDC_DEVICE_ID_REG                0xFFU
#define FDC_MANUFACTURER_ID_VALUE        0x5449U
#define FDC_DEVICE_ID_VALUE              0x1004U
#define FDC_CAPDAC_CODE                     0U
#define FDC_CHB_CAPDAC                   0x1000U
#define FDC_CAPDAC_SHIFT                     5U
#define FDC_SINGLE_MEAS1_100SPS           0x0480U
#define FDC_MEAS1_DONE_MASK              0x0008U
#define FDC_RAW_PER_PF                  524288LL
#define FDC_PF_SCALE                     10000LL
#define FDC_CAPDAC_STEP_PF_X10000        31250L

#define EVENT_LED_1                      BIT(0)
#define EVENT_ACCEL                      BIT(1)
#define EVENT_FDC                        BIT(2)
#define EVENT_BATTERY                    BIT(3)
#define EVENT_USB_OUTPUT                 BIT(4)

BUILD_ASSERT(APP_TIMER_TICK_MS > 0U, "Timer tick must be positive");
BUILD_ASSERT(BUTTON_DEBOUNCE_MS % APP_TIMER_TICK_MS == 0U, "Debounce/tick mismatch");
BUILD_ASSERT(DEBUG_LED_1_TOGGLE_MS % APP_TIMER_TICK_MS == 0U, "LED1/tick mismatch");
BUILD_ASSERT(DEBUG_LED_2_TOGGLE_MS % APP_TIMER_TICK_MS == 0U, "LED2/tick mismatch");
BUILD_ASSERT(ACCEL_SAMPLE_PERIOD_MS % APP_TIMER_TICK_MS == 0U, "Accel/tick mismatch");
BUILD_ASSERT(FDC_SAMPLE_PERIOD_MS % APP_TIMER_TICK_MS == 0U, "FDC/tick mismatch");
BUILD_ASSERT(BATTERY_SAMPLE_PERIOD_MS % APP_TIMER_TICK_MS == 0U, "ADC/tick mismatch");
BUILD_ASSERT(USB_OUTPUT_PERIOD_MS % APP_TIMER_TICK_MS == 0U, "USB/tick mismatch");
BUILD_ASSERT(USB_OUTPUT_DIAGNOSTICS <= 1U, "Diagnostics must be 0 or 1");
BUILD_ASSERT(USB_OUTPUT_MEAL_GATES <= 1U, "Meal gate display must be 0 or 1");
BUILD_ASSERT(FDC_CAPDAC_CODE <= 31U, "CAPDAC must be 0..31");
BUILD_ASSERT(BATTERY_DIVIDER_LOW_OHM > 0U, "Divider denominator must be positive");

BUILD_ASSERT(MEAL_MOTION_TRIGGER_MG > 0L, "Motion threshold must be positive");
BUILD_ASSERT(MEAL_MOTION_MIN_SAMPLES >= 2U, "Motion needs distinct measurements");
BUILD_ASSERT(MEAL_MOTION_MIN_SAMPLES <= MEAL_MOTION_HISTORY_SAMPLES,
             "Motion history must cover minimum sample count");
BUILD_ASSERT(MEAL_MOTION_FILTER_SAMPLES > 0U, "Motion filter must not be empty");
BUILD_ASSERT(MEAL_MOTION_HISTORY_SAMPLES > 0U, "Motion history must not be empty");
BUILD_ASSERT(MEAL_CONTACT_OFF_PF_X10000 < MEAL_CONTACT_ON_PF_X10000,
             "Meal contact hysteresis");

/* Private types */
typedef enum {
    MEAL_STATE_WAIT_READY = 0,
    MEAL_STATE_READY,
    MEAL_STATE_EATING,
    MEAL_STATE_RETURN,
    MEAL_STATE_ERROR,
} meal_state_t;

typedef struct {
    meal_state_t state;
    uint32_t count;
    bool is_confirmation_pending;
    uint32_t confirmation_since_ms;
    bool has_fdc_sample;
    uint32_t last_fdc_sample_ms;
} meal_status_t;

typedef enum {
    ACCEL_STATE_NOT_STARTED = 0,
    ACCEL_STATE_RESET_WAIT,
    ACCEL_STATE_SETTLING,
    ACCEL_STATE_READY,
    ACCEL_STATE_FAILED,
} accel_state_t;

typedef struct {
    int init_ret;
    int sample_ret;
    bool has_sample;
    uint32_t sample_ms;
} sample_status_t;

typedef struct {
    bool is_candidate_pressed;
    bool is_pressed;
    uint32_t candidate_since_ms;
    uint32_t hold_ms;
    int raw_level;
    int read_ret;
} button_status_t;

typedef struct {
    sample_status_t status;
    accel_state_t state;
    uint8_t address;
    uint8_t who_am_i;
    uint32_t state_since_ms;
    int32_t x_mg;
    int32_t y_mg;
    int32_t z_mg;
} accel_status_t;

typedef struct {
    sample_status_t status;
    uint16_t manufacturer_id;
    uint16_t device_id;
    bool is_conversion_pending;
    uint32_t trigger_ms;
    int32_t capacitance_pf_x10000;
} fdc_status_t;

typedef struct {
    bool has_sample;
    uint32_t sample_ms;
    int32_t filter[MEAL_MOTION_FILTER_SAMPLES][3];
    int64_t filter_sum[3];
    uint32_t filter_index;
    uint32_t filter_count;
    int32_t history[MEAL_MOTION_HISTORY_SAMPLES][3];
    uint32_t history_ms[MEAL_MOTION_HISTORY_SAMPLES];
    uint32_t history_index;
    uint32_t history_count;
    bool has_motion;
    uint32_t motion_ms;
    int64_t span_mg;
} meal_motion_status_t;

/* Static variables */
static const struct gpio_dt_spec s_debug_led_1 =
    GPIO_DT_SPEC_GET(DT_ALIAS(debug_led_1), gpios);
static const struct gpio_dt_spec s_debug_led_2 =
    GPIO_DT_SPEC_GET(DT_ALIAS(debug_led_2), gpios);
static const struct gpio_dt_spec s_meal_led_green =
    GPIO_DT_SPEC_GET(DT_ALIAS(meal_led_green), gpios);
static const struct gpio_dt_spec s_meal_led_blue =
    GPIO_DT_SPEC_GET(DT_ALIAS(meal_led_blue), gpios);
static const struct gpio_dt_spec s_power_hold =
    GPIO_DT_SPEC_GET(DT_ALIAS(power_hold), gpios);
static const struct gpio_dt_spec s_power_button =
    GPIO_DT_SPEC_GET(DT_ALIAS(power_button), gpios);
static const struct adc_dt_spec s_battery_adc =
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), battery);
static const struct device *const s_p_timer = DEVICE_DT_GET(DT_NODELABEL(timer2));
static const struct device *const s_p_accel_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct device *const s_p_fdc_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static const struct device *const s_p_usb = DEVICE_DT_GET(DT_NODELABEL(usb_cdc));

static struct k_sem s_tick_sem;
static atomic_t s_timer_tick_count;
static atomic_t s_timer_events;
static atomic_t s_led_2_runtime_ret;
static bool s_is_led_2_ready;
static uint32_t s_missed_ticks;
static uint32_t s_usb_dropped_rows;
static int s_led_1_ret;
static int s_led_2_ret;
static int s_meal_led_green_ret;
static int s_meal_led_blue_ret;
static meal_status_t s_meal;
static meal_motion_status_t s_meal_motion;
static int s_button_init_ret;
static int s_power_init_ret;
static int s_power_set_ret = -EAGAIN;
static int s_timer_ret;
static int s_usb_ret;
static bool s_is_power_held;
static bool s_has_power_hold_attempted;
static bool s_has_sensor_init_started;
static bool s_is_usb_connected;
static bool s_should_print_header = true;
static button_status_t s_button;
static sample_status_t s_battery_status = {
    .init_ret = -EINPROGRESS,
    .sample_ret = -ENODATA,
};
static accel_status_t s_accel = {
    .status = { .init_ret = -EINPROGRESS, .sample_ret = -ENODATA },
};
static fdc_status_t s_fdc = {
    .status = { .init_ret = -EINPROGRESS, .sample_ret = -ENODATA },
};
static int16_t s_battery_adc_raw;
static int32_t s_battery_adc_mv;
static int32_t s_battery_mv;
static struct adc_sequence s_battery_sequence = {
    .buffer = &s_battery_adc_raw,
    .buffer_size = sizeof(s_battery_adc_raw),
    .calibrate = true,
};
static uint8_t s_usb_tx_storage[USB_TX_BUFFER_BYTES];
static struct ring_buf s_usb_tx_ring;

/* Static function prototypes */
static uint32_t app_get_time_ms(void);
static int32_t value_divide_round(int64_t numerator, int32_t denominator);
static int32_t accel_decode_mg(const uint8_t *p_bytes);
static int32_t fdc_decode_pf_x10000(uint16_t msb, uint16_t lsb);
static int gpio_output_init(const struct gpio_dt_spec *p_gpio);
static void gpio_init(void);
static void app_timer_isr_handler(const struct device *p_dev, void *p_user_data);
static int app_timer_init(void);
static void button_process(uint32_t now_ms);
static int battery_init(void);
static void battery_process(uint32_t now_ms);
static int accel_init(uint32_t now_ms);
static int accel_configure(void);
static void accel_init_process(uint32_t now_ms);
static void accel_process(uint32_t now_ms);
static int fdc_read_register(uint8_t reg, uint16_t *p_value);
static int fdc_write_register(uint8_t reg, uint16_t value);
static int fdc_init(void);
static void fdc_process(uint32_t now_ms, bool should_trigger);
static void sensor_init(uint32_t now_ms);
static void usb_uart_callback(const struct device *p_dev, void *p_user_data);
static int usb_init(void);
static void usb_connection_process(void);
static bool usb_queue_text(const char *p_text, size_t length_bytes);
static const char *init_get_label(int ret);
static const char *accel_get_name(void);
static void usb_print_header(void);
static int sample_get_ret(const sample_status_t *p_status, uint32_t now_ms,
                          uint32_t stale_ms);
static void value_format(char *p_text, size_t capacity_bytes, bool has_value, int32_t value);
static void usb_print_sample(uint32_t now_ms);
static void meal_motion_process(uint32_t now_ms);
static bool meal_motion_is_recent(uint32_t now_ms);
static bool meal_fdc_is_usable(uint32_t now_ms);
static void value_format_pf(char *p_text, size_t capacity_bytes, int32_t value);
static const char *meal_get_pattern(uint32_t now_ms);
static void meal_update_leds(void);
static void meal_process(uint32_t now_ms);

/* Public functions */
int main(void)
{
    uint32_t previous_ticks = 0U;

    int init_ret = k_sem_init(&s_tick_sem, 0U, 1U);
    if (init_ret < 0) {
        return init_ret;
    }
    gpio_init();
    s_timer_ret = app_timer_init();
    s_usb_ret = usb_init();

    if (s_timer_ret < 0) {
        /* No software-timer fallback: an invalid hardware timer is a fatal setup error. */
        return s_timer_ret;
    }

    while (true) {
        int ret = k_sem_take(&s_tick_sem, K_FOREVER);
        if (ret < 0) {
            continue;
        }

        unsigned int key = irq_lock();
        uint32_t ticks = (uint32_t)atomic_get(&s_timer_tick_count);
        uint32_t events = (uint32_t)atomic_set(&s_timer_events, 0);
        irq_unlock(key);
        uint32_t elapsed_ticks = ticks - previous_ticks;
        uint32_t now_ms = ticks * APP_TIMER_TICK_MS;
        previous_ticks = ticks;

        if (elapsed_ticks > 1U) {
            s_missed_ticks += elapsed_ticks - 1U;
        }

        /* Power qualification always precedes sensor I/O and USB formatting. */
        button_process(now_ms);
        usb_connection_process();

        if ((events & EVENT_LED_1) != 0U && s_led_1_ret == 0) {
            s_led_1_ret = gpio_pin_toggle_dt(&s_debug_led_1);
        }

        if (s_is_power_held && !s_has_sensor_init_started) {
            sensor_init(now_ms);
        }

        if (s_has_sensor_init_started) {
            accel_init_process(now_ms);
            if ((events & EVENT_ACCEL) != 0U) {
                accel_process(now_ms);
            }
            fdc_process(now_ms, (events & EVENT_FDC) != 0U);
            if ((events & EVENT_BATTERY) != 0U) {
                battery_process(now_ms);
            }
        }

        now_ms = app_get_time_ms();
        meal_motion_process(now_ms);
        meal_process(now_ms);
        if (s_is_usb_connected && s_should_print_header) {
            usb_print_header();
        }
        if ((events & EVENT_USB_OUTPUT) != 0U && s_is_usb_connected &&
            !s_should_print_header) {
            usb_print_sample(now_ms);
        }
    }
}

/* Private functions */
static uint32_t app_get_time_ms(void)
{
    return (uint32_t)atomic_get(&s_timer_tick_count) * APP_TIMER_TICK_MS;
}

static int32_t value_divide_round(int64_t numerator, int32_t denominator)
{
    int64_t half = denominator / 2;
    return (int32_t)((numerator >= 0) ?
        ((numerator + half) / denominator) : ((numerator - half) / denominator));
}

static int32_t accel_decode_mg(const uint8_t *p_bytes)
{
    int32_t raw = (int32_t)(((uint16_t)p_bytes[1] << 8) | p_bytes[0]);
    if (raw >= 32768L) {
        raw -= 65536L;
    }
    if (s_accel.who_am_i == ACCEL_ID_LIS2DH12) {
        /* +/-2 g HR: 12-bit sample, left aligned by 4 bits, 1 mg/count. */
        return value_divide_round(raw, 16);
    }
    /* ID 0x44, +/-2 g HP: left-aligned output sensitivity is 0.061 mg/LSB. */
    return value_divide_round((int64_t)raw * 61LL, 1000);
}

static int32_t fdc_decode_pf_x10000(uint16_t msb, uint16_t lsb)
{
    uint32_t raw_unsigned = ((uint32_t)msb << 8) | ((uint32_t)lsb >> 8);
    int32_t raw = (int32_t)raw_unsigned;
    if ((raw_unsigned & 0x00800000UL) != 0U) {
        raw -= 16777216L;
    }
    return value_divide_round((int64_t)raw * FDC_PF_SCALE, (int32_t)FDC_RAW_PER_PF)
        + ((int32_t)FDC_CAPDAC_CODE * FDC_CAPDAC_STEP_PF_X10000);
}

static int gpio_output_init(const struct gpio_dt_spec *p_gpio)
{
    if (!gpio_is_ready_dt(p_gpio)) {
        return -ENODEV;
    }
    return gpio_pin_configure_dt(p_gpio, GPIO_OUTPUT_INACTIVE);
}

static void gpio_init(void)
{
    s_power_init_ret = gpio_output_init(&s_power_hold);
    s_led_1_ret = gpio_output_init(&s_debug_led_1);
    s_led_2_ret = gpio_output_init(&s_debug_led_2);
    s_is_led_2_ready = (s_led_2_ret == 0);
    s_meal_led_green_ret = gpio_output_init(&s_meal_led_green);
    s_meal_led_blue_ret = gpio_output_init(&s_meal_led_blue);
    meal_update_leds();
    s_button_init_ret = gpio_is_ready_dt(&s_power_button) ?
        gpio_pin_configure_dt(&s_power_button, GPIO_INPUT) : -ENODEV;
}

static void app_timer_isr_handler(const struct device *p_dev, void *p_user_data)
{
    static uint32_t s_led_1_ticks;
    static uint32_t s_led_2_ticks;
    static uint32_t s_accel_ticks;
    static uint32_t s_fdc_ticks;
    static uint32_t s_battery_ticks;
    static uint32_t s_usb_ticks;
    uint32_t events = 0U;

    ARG_UNUSED(p_dev);
    ARG_UNUSED(p_user_data);
    atomic_inc(&s_timer_tick_count);

    if (++s_led_1_ticks >= DEBUG_LED_1_TOGGLE_MS / APP_TIMER_TICK_MS) {
        s_led_1_ticks = 0U;
        events |= EVENT_LED_1;
    }
    if (++s_led_2_ticks >= DEBUG_LED_2_TOGGLE_MS / APP_TIMER_TICK_MS) {
        s_led_2_ticks = 0U;
        /* Native nRF GPIO only: LED2 proves the hardware timer IRQ is running. */
        if (s_is_led_2_ready && atomic_get(&s_led_2_runtime_ret) == 0) {
            int ret = gpio_pin_toggle_dt(&s_debug_led_2);
            if (ret < 0) {
                atomic_set(&s_led_2_runtime_ret, ret);
            }
        }
    }
    if (++s_accel_ticks >= ACCEL_SAMPLE_PERIOD_MS / APP_TIMER_TICK_MS) {
        s_accel_ticks = 0U;
        events |= EVENT_ACCEL;
    }
    if (++s_fdc_ticks >= FDC_SAMPLE_PERIOD_MS / APP_TIMER_TICK_MS) {
        s_fdc_ticks = 0U;
        events |= EVENT_FDC;
    }
    if (++s_battery_ticks >= BATTERY_SAMPLE_PERIOD_MS / APP_TIMER_TICK_MS) {
        s_battery_ticks = 0U;
        events |= EVENT_BATTERY;
    }
    if (++s_usb_ticks >= USB_OUTPUT_PERIOD_MS / APP_TIMER_TICK_MS) {
        s_usb_ticks = 0U;
        events |= EVENT_USB_OUTPUT;
    }

    atomic_or(&s_timer_events, (atomic_val_t)events);
    k_sem_give(&s_tick_sem);
}

static int app_timer_init(void)
{
    if (!device_is_ready(s_p_timer)) {
        return -ENODEV;
    }

    struct counter_top_cfg config = {
        .ticks = counter_us_to_ticks(s_p_timer, APP_TIMER_TICK_MS * 1000ULL),
        .callback = app_timer_isr_handler,
        .user_data = NULL,
        .flags = 0U,
    };

    if (config.ticks == 0U || config.ticks > counter_get_max_top_value(s_p_timer)) {
        return -EINVAL;
    }
    int ret = counter_set_top_value(s_p_timer, &config);
    if (ret < 0) {
        return ret;
    }
    return counter_start(s_p_timer);
}

static void button_process(uint32_t now_ms)
{
    if (s_button_init_ret < 0) {
        s_button.read_ret = s_button_init_ret;
        return;
    }

    int level = gpio_pin_get_raw(s_power_button.port, s_power_button.pin);
    if (level < 0) {
        s_button.read_ret = level;
        s_button.is_pressed = false;
        s_button.is_candidate_pressed = false;
        s_button.candidate_since_ms = now_ms;
        s_button.hold_ms = 0U;
        return;
    }

    bool is_pressed = (level != 0); /* Circuit and overlay: active HIGH. */
    s_button.raw_level = level;
    s_button.read_ret = 0;
    if (is_pressed != s_button.is_candidate_pressed) {
        s_button.is_candidate_pressed = is_pressed;
        s_button.candidate_since_ms = now_ms;
    }

    if (is_pressed != s_button.is_pressed &&
        now_ms - s_button.candidate_since_ms >= BUTTON_DEBOUNCE_MS) {
        s_button.is_pressed = is_pressed;
    }

    /* A sampled release immediately cancels qualification, even during debounce. */
    uint32_t candidate_ms = now_ms - s_button.candidate_since_ms;
    s_button.hold_ms = (s_button.is_pressed && is_pressed &&
        candidate_ms >= BUTTON_DEBOUNCE_MS) ? candidate_ms : 0U;

    if (!s_is_power_held && !is_pressed) {
        s_has_power_hold_attempted = false;
        s_power_set_ret = -EAGAIN;
    }
    if (!s_is_power_held && s_button.hold_ms >= POWER_HOLD_PRESS_MS &&
        s_power_init_ret == 0 && !s_has_power_hold_attempted) {
        s_has_power_hold_attempted = true;
        s_power_set_ret = gpio_pin_set_dt(&s_power_hold, 1);
        if (s_power_set_ret == 0) {
            s_is_power_held = true;
            s_should_print_header = true;
        }
    }
}

static int battery_init(void)
{
    if (!adc_is_ready_dt(&s_battery_adc)) {
        return -ENODEV;
    }
    int ret = adc_channel_setup_dt(&s_battery_adc);
    if (ret < 0) {
        return ret;
    }
    return adc_sequence_init_dt(&s_battery_adc, &s_battery_sequence);
}

static void battery_process(uint32_t now_ms)
{
    if (s_battery_status.init_ret < 0) {
        return;
    }

    int ret = adc_read_dt(&s_battery_adc, &s_battery_sequence);
    int32_t pin_mv = s_battery_adc_raw;
    if (ret == 0) {
        ret = adc_raw_to_millivolts_dt(&s_battery_adc, &pin_mv);
    }
    if (ret == 0 && pin_mv < 0) {
        ret = -ERANGE;
    }

    s_battery_status.sample_ret = ret;
    s_battery_status.has_sample = (ret == 0);
    if (ret == 0) {
        s_battery_sequence.calibrate = false;
        s_battery_adc_mv = pin_mv;
        s_battery_mv = value_divide_round(
            (int64_t)pin_mv * (BATTERY_DIVIDER_HIGH_OHM + BATTERY_DIVIDER_LOW_OHM),
            (int32_t)BATTERY_DIVIDER_LOW_OHM);
        s_battery_status.sample_ms = now_ms;
    }
}

static int accel_init(uint32_t now_ms)
{
    const uint8_t addresses[] = { ACCEL_ADDRESS_FIRST, ACCEL_ADDRESS_SECOND };
    int last_ret = -ENODEV;
    bool has_response = false;

    s_accel.address = 0U;
    s_accel.who_am_i = 0U;

    if (!device_is_ready(s_p_accel_i2c)) {
        return -ENODEV;
    }
    for (size_t i = 0U; i < ARRAY_SIZE(addresses); i++) {
        uint8_t id = 0U;
        int ret = i2c_reg_read_byte(s_p_accel_i2c, addresses[i], ACCEL_WHO_AM_I_REG, &id);
        if (ret < 0) {
            if (!has_response) {
                last_ret = ret;
            }
            continue;
        }
        has_response = true;
        s_accel.address = addresses[i];
        s_accel.who_am_i = id;
        if (id != ACCEL_ID_LIS2DH12 && id != ACCEL_ID_LIS2DW12_FAMILY) {
            last_ret = -ENOTSUP;
            continue;
        }
        if (id == ACCEL_ID_LIS2DH12) {
            ret = accel_configure();
            if (ret < 0) {
                return ret;
            }
            s_accel.state = ACCEL_STATE_SETTLING;
            s_accel.state_since_ms = now_ms;
            return -EINPROGRESS;
        }
        ret = i2c_reg_write_byte(s_p_accel_i2c, addresses[i], ACCEL_CTRL2_REG,
                                 ACCEL_SOFT_RESET);
        if (ret < 0) {
            return ret;
        }
        s_accel.state = ACCEL_STATE_RESET_WAIT;
        s_accel.state_since_ms = now_ms;
        return -EINPROGRESS;
    }
    return last_ret;
}

static int accel_configure(void)
{
    const uint8_t dw_settings[][2] = {
        { ACCEL_CTRL2_REG, ACCEL_BDU_AUTO_INCREMENT },
        { ACCEL_CTRL6_REG, ACCEL_2G_ODR_DIV2 },
        { ACCEL_FIFO_CTRL_REG, 0U },
        { ACCEL_CTRL1_REG, ACCEL_50HZ_HIGH_PERFORMANCE },
    };
    const uint8_t dh_settings[][2] = {
        { ACCEL_CTRL1_REG, 0U },
        { ACCEL_CTRL2_REG, 0U },
        { ACCEL_CTRL3_REG, 0U },
        { ACCEL_CTRL4_REG, ACCEL_DH_BDU_HR_2G },
        { ACCEL_CTRL5_REG, 0U },
        { ACCEL_CTRL6_REG, 0U },
        { ACCEL_FIFO_CTRL_REG, 0U },
        { ACCEL_CTRL1_REG, ACCEL_DH_50HZ_XYZ },
    };
    bool is_dh = (s_accel.who_am_i == ACCEL_ID_LIS2DH12);
    const uint8_t (*p_settings)[2] = is_dh ? dh_settings : dw_settings;
    size_t count = is_dh ? ARRAY_SIZE(dh_settings) : ARRAY_SIZE(dw_settings);

    for (size_t i = 0U; i < count; i++) {
        int ret = i2c_reg_write_byte(s_p_accel_i2c, s_accel.address,
                                     p_settings[i][0], p_settings[i][1]);
        if (ret < 0) {
            return ret;
        }
    }

    uint8_t ctrl1 = 0U;
    int ret = i2c_reg_read_byte(s_p_accel_i2c, s_accel.address, ACCEL_CTRL1_REG, &ctrl1);
    if (ret < 0) {
        return ret;
    }
    uint8_t expected = is_dh ? ACCEL_DH_50HZ_XYZ : ACCEL_50HZ_HIGH_PERFORMANCE;
    return (ctrl1 == expected) ? 0 : -EIO;
}

static void accel_init_process(uint32_t now_ms)
{
    if (s_accel.state == ACCEL_STATE_RESET_WAIT) {
        uint8_t ctrl2 = 0U;
        int ret = i2c_reg_read_byte(s_p_accel_i2c, s_accel.address, ACCEL_CTRL2_REG, &ctrl2);
        if (ret == 0 && (ctrl2 & ACCEL_SOFT_RESET) != 0U) {
            if (now_ms - s_accel.state_since_ms < ACCEL_RESET_TIMEOUT_MS) {
                return;
            }
            ret = -ETIMEDOUT;
        }
        if (ret == 0) {
            ret = accel_configure();
        }
        if (ret < 0) {
            s_accel.status.init_ret = ret;
            s_accel.state = ACCEL_STATE_FAILED;
            s_should_print_header = true;
            return;
        }
        s_accel.state = ACCEL_STATE_SETTLING;
        s_accel.state_since_ms = now_ms;
    }

    uint32_t settle_ms = (s_accel.who_am_i == ACCEL_ID_LIS2DH12) ?
        ACCEL_DH_SETTLE_MS : ACCEL_SETTLE_MS;
    if (s_accel.state == ACCEL_STATE_SETTLING &&
        now_ms - s_accel.state_since_ms >= settle_ms) {
        s_accel.status.init_ret = 0;
        s_accel.state = ACCEL_STATE_READY;
        s_should_print_header = true;
    }
}

static void accel_process(uint32_t now_ms)
{
    if (s_accel.state != ACCEL_STATE_READY) {
        return;
    }

    uint8_t status = 0U;
    uint8_t data[6];
    bool is_dh = (s_accel.who_am_i == ACCEL_ID_LIS2DH12);
    uint8_t ready_mask = is_dh ? ACCEL_DH_DATA_READY_MASK : ACCEL_DATA_READY_MASK;
    uint8_t output_reg = is_dh ? (ACCEL_OUTPUT_REG | ACCEL_DH_AUTO_INCREMENT) :
        ACCEL_OUTPUT_REG;
    int ret = i2c_reg_read_byte(s_p_accel_i2c, s_accel.address, ACCEL_STATUS_REG, &status);
    if (ret == 0 && (status & ready_mask) == 0U) {
        /* Keep the last complete sample; age checks expose a stalled sensor. */
        return;
    }
    if (ret == 0) {
        ret = i2c_burst_read(s_p_accel_i2c, s_accel.address, output_reg,
                             data, sizeof(data));
    }
    s_accel.status.sample_ret = ret;
    s_accel.status.has_sample = (ret == 0);
    if (ret == 0) {
        s_accel.x_mg = accel_decode_mg(&data[0]);
        s_accel.y_mg = accel_decode_mg(&data[2]);
        s_accel.z_mg = accel_decode_mg(&data[4]);
        s_accel.status.sample_ms = now_ms;
    }
}

static int fdc_read_register(uint8_t reg, uint16_t *p_value)
{
    uint8_t data[2];
    int ret = i2c_write_read(s_p_fdc_i2c, FDC_ADDRESS, &reg, sizeof(reg), data, sizeof(data));
    if (ret < 0) {
        return ret;
    }
    *p_value = ((uint16_t)data[0] << 8) | data[1];
    return 0;
}

static int fdc_write_register(uint8_t reg, uint16_t value)
{
    uint8_t data[] = { reg, (uint8_t)(value >> 8), (uint8_t)value };
    return i2c_write(s_p_fdc_i2c, data, sizeof(data), FDC_ADDRESS);
}

static int fdc_init(void)
{
    if (!device_is_ready(s_p_fdc_i2c)) {
        return -ENODEV;
    }
    int ret = fdc_read_register(FDC_MANUFACTURER_ID_REG, &s_fdc.manufacturer_id);
    if (ret == 0) {
        ret = fdc_read_register(FDC_DEVICE_ID_REG, &s_fdc.device_id);
    }
    if (ret < 0) {
        return ret;
    }
    if (s_fdc.manufacturer_id != FDC_MANUFACTURER_ID_VALUE ||
        s_fdc.device_id != FDC_DEVICE_ID_VALUE) {
        return -ENODEV;
    }
    /* Single shots avoid mixing two conversions between separate MSB/LSB reads. */
    ret = fdc_write_register(FDC_CONFIG_REG, 0U);
    if (ret == 0) {
        ret = fdc_write_register(FDC_MEAS1_CONFIG_REG,
            FDC_CHB_CAPDAC | ((uint16_t)FDC_CAPDAC_CODE << FDC_CAPDAC_SHIFT));
    }
    return ret;
}

static void fdc_process(uint32_t now_ms, bool should_trigger)
{
    if (s_fdc.status.init_ret < 0) {
        return;
    }

    if (s_fdc.is_conversion_pending) {
        uint16_t status = 0U;
        uint16_t msb = 0U;
        uint16_t lsb = 0U;
        int ret = fdc_read_register(FDC_CONFIG_REG, &status);
        if (ret == 0 && (status & FDC_MEAS1_DONE_MASK) == 0U) {
            if (now_ms - s_fdc.trigger_ms < FDC_CONVERSION_TIMEOUT_MS) {
                return;
            }
            ret = -ETIMEDOUT;
        }
        if (ret == 0) {
            ret = fdc_read_register(FDC_MEAS1_MSB_REG, &msb);
        }
        if (ret == 0) {
            ret = fdc_read_register(FDC_MEAS1_LSB_REG, &lsb);
        }
        s_fdc.is_conversion_pending = false;
        s_fdc.status.sample_ret = ret;
        s_fdc.status.has_sample = (ret == 0);
        if (ret == 0) {
            s_fdc.capacitance_pf_x10000 = fdc_decode_pf_x10000(msb, lsb);
            s_fdc.status.sample_ms = now_ms;
            int32_t residual = s_fdc.capacitance_pf_x10000 -
                ((int32_t)FDC_CAPDAC_CODE * FDC_CAPDAC_STEP_PF_X10000);
            if (residual <= -150000L || residual >= 150000L) {
                s_fdc.status.sample_ret = -ERANGE;
            }
        }
    }

    if (should_trigger && !s_fdc.is_conversion_pending) {
        int ret = fdc_write_register(FDC_CONFIG_REG, FDC_SINGLE_MEAS1_100SPS);
        if (ret < 0) {
            s_fdc.status.sample_ret = ret;
            s_fdc.status.has_sample = false;
        } else {
            s_fdc.is_conversion_pending = true;
            s_fdc.trigger_ms = now_ms;
        }
    }
}

static void sensor_init(uint32_t now_ms)
{
    s_has_sensor_init_started = true;
    s_battery_status.init_ret = battery_init();
    s_accel.status.init_ret = accel_init(now_ms);
    if (s_accel.status.init_ret != -EINPROGRESS) {
        s_accel.state = ACCEL_STATE_FAILED;
    }
    s_fdc.status.init_ret = fdc_init();
    battery_process(now_ms);
    fdc_process(now_ms, true);
    s_should_print_header = true;
}

static void usb_uart_callback(const struct device *p_dev, void *p_user_data)
{
    ARG_UNUSED(p_user_data);
    for (uint32_t i = 0U; i < USB_IRQ_BUDGET; i++) {
        if (!uart_irq_update(p_dev) || !uart_irq_is_pending(p_dev)) {
            break;
        }
        if (uart_irq_rx_ready(p_dev)) {
            uint8_t discarded[64];
            int ret = uart_fifo_read(p_dev, discarded, sizeof(discarded));
            if (ret < 0) {
                uart_irq_rx_disable(p_dev);
            }
        }
        if (uart_irq_tx_ready(p_dev)) {
            uint8_t *p_data = NULL;
            uint32_t length = ring_buf_get_claim(&s_usb_tx_ring, &p_data, 128U);
            if (length == 0U) {
                uart_irq_tx_disable(p_dev);
                break;
            }
            int written = uart_fifo_fill(p_dev, p_data, (int)length);
            uint32_t consumed = (written > 0) ? (uint32_t)written : 0U;
            int ret = ring_buf_get_finish(&s_usb_tx_ring, consumed);
            if (ret < 0 || written < 0) {
                uart_irq_tx_disable(p_dev);
                break;
            }
            if (written == 0) {
                break;
            }
        }
    }
}

static int usb_init(void)
{
    ring_buf_init(&s_usb_tx_ring, sizeof(s_usb_tx_storage), s_usb_tx_storage);
    if (!device_is_ready(s_p_usb)) {
        return -ENODEV;
    }
    int ret = uart_irq_callback_user_data_set(s_p_usb, usb_uart_callback, NULL);
    if (ret < 0) {
        return ret;
    }
    ret = usb_enable(NULL);
    if (ret < 0 && ret != -EALREADY) {
        return ret;
    }
    uart_irq_rx_enable(s_p_usb);
    return 0;
}

static void usb_connection_process(void)
{
    if (s_usb_ret < 0) {
        return;
    }
    uint32_t dtr = 0U;
    int ret = uart_line_ctrl_get(s_p_usb, UART_LINE_CTRL_DTR, &dtr);
    bool is_connected = (ret == 0 && dtr != 0U);
    if (is_connected != s_is_usb_connected) {
        /* One producer/one consumer; reset only with callback preemption excluded. */
        unsigned int key = irq_lock();
        uart_irq_tx_disable(s_p_usb);
        ring_buf_reset(&s_usb_tx_ring);
        irq_unlock(key);
        s_is_usb_connected = is_connected;
        s_should_print_header = true;
    }
}

static bool usb_queue_text(const char *p_text, size_t length_bytes)
{
    if (!s_is_usb_connected || length_bytes > USB_TX_BUFFER_BYTES ||
        ring_buf_space_get(&s_usb_tx_ring) < length_bytes) {
        return false;
    }
    /* Only main produces; the UART callback consumes. Never wait for a host. */
    uint32_t written = ring_buf_put(&s_usb_tx_ring, (const uint8_t *)p_text,
                                    (uint32_t)length_bytes);
    if (written != length_bytes) {
        return false;
    }
    uart_irq_tx_enable(s_p_usb);
    return true;
}

static const char *init_get_label(int ret)
{
    if (ret == 0) {
        return "OK";
    }
    return (ret == -EINPROGRESS) ? "WAIT" : "FAIL";
}

static const char *accel_get_name(void)
{
    if (s_accel.who_am_i == ACCEL_ID_LIS2DH12) {
        return "LIS2DH12";
    }
    return (s_accel.who_am_i == ACCEL_ID_LIS2DW12_FAMILY) ?
        "LIS2DW12/DTW12" : "NOT DETECTED";
}

static void usb_print_header(void)
{
    char text[USB_HEADER_BUFFER_BYTES];
    int length = snprintf(text, sizeof(text),
        "\r\n============================================================\r\n"
        " APSLIM MEAL COUNT TEST | MOTION-V3 | 20261024\r\n"
        "============================================================\r\n"
        "[01 SYSTEM / POWER]\r\n"
        "  BTN   P0.26 : %-4s (%d) | Active HIGH\r\n"
        "  HOLD  P0.05 : %-4s (%d) | Output=%s\r\n"
        "  STATE       : %s | Hold BTN for 3000 ms to turn ON\r\n"
        "\r\n[02 TIMER / LED / USB]\r\n"
        "  TIMER2      : %-4s (%d) | Base=%u ms\r\n"
        "  LED1 P0.31  : %-4s (%d) | Toggle=%u ms [MAIN]\r\n"
        "  LED2 P0.30  : %-4s (%d) | Toggle=%u ms [TIMER ISR]\r\n"
        "  USB CDC     : %-4s (%d) | Print=%u ms | Detail=%u\r\n"
        "\r\n[03 SENSOR INITIALIZATION]\r\n"
        "  BATTERY ADC : %-4s (%d) | P0.02 | Sample=%u ms\r\n"
        "  ACCEL       : %-4s (%d) | %s\r\n"
        "    I2C0: ADDR(7bit)=0x%02X | WHO_AM_I=0x%02X | Sample=%u ms\r\n"
        "  FDC1004     : %-4s (%d) | I2C1 ADDR=0x50 | CIN1\r\n"
        "    MFR=0x%04X | ID=0x%04X | CAPDAC=%u | Sample=%u ms\r\n"
        "\r\n[MEAL DETECTION]\r\n"
        "  RGB G=P0.11: %s (%d) | B=P0.12: %s (%d) | Active LOW\r\n"
        "  Any-axis motion: ON=%ld mg | Hold=%u ms | Rearm=%u ms released\r\n"
        "  ACC filter=%u samples | Window=%u ms | Not a yaw angle detector\r\n"
        "  FDC BASE=%ld/10000 pF | ON=%ld/10000 pF | OFF=%ld/10000 pF\r\n"
        "  Contact confirm=%u ms | GATE: C=contact M=recent-motion R=release\r\n"
        "  EATING=GREEN | Other=BLUE | Release continuously before next count\r\n"
        "  Positive FDC saturation is accepted as contact in this test\r\n"
        "\r\n[04 LIVE DATA]\r\n"
        "  [BTN] | [BAT mV] | [ACC X/Y/Z mg] | [FDC pF + delta] | [MOT mg] | [MEAL] | [COUNT]\r\n"
        "  Live timestamps omitted | Pending application TX: skip new display row\r\n"
        "  BTN: 1=PRESSED, 0=RELEASED | NA=not ready/error/stale\r\n"
        "  WAIT=not started/initializing | FAIL=error; see code in parentheses\r\n"
        "============================================================\r\n",
        init_get_label(s_button_init_ret), s_button_init_ret,
        init_get_label(s_power_init_ret), s_power_init_ret,
        s_is_power_held ? "HIGH" : "LOW",
        s_is_power_held ? "ON" : "WAIT_BTN_3S",
        init_get_label(s_timer_ret), s_timer_ret, APP_TIMER_TICK_MS,
        init_get_label(s_led_1_ret), s_led_1_ret, DEBUG_LED_1_TOGGLE_MS,
        init_get_label(s_led_2_ret), s_led_2_ret, DEBUG_LED_2_TOGGLE_MS,
        init_get_label(s_usb_ret), s_usb_ret, USB_OUTPUT_PERIOD_MS,
        (unsigned int)USB_OUTPUT_DIAGNOSTICS,
        init_get_label(s_battery_status.init_ret), s_battery_status.init_ret,
        BATTERY_SAMPLE_PERIOD_MS,
        init_get_label(s_accel.status.init_ret), s_accel.status.init_ret, accel_get_name(),
        (unsigned int)s_accel.address, (unsigned int)s_accel.who_am_i,
        ACCEL_SAMPLE_PERIOD_MS,
        init_get_label(s_fdc.status.init_ret), s_fdc.status.init_ret,
        (unsigned int)s_fdc.manufacturer_id, (unsigned int)s_fdc.device_id,
        (unsigned int)FDC_CAPDAC_CODE, FDC_SAMPLE_PERIOD_MS,
        init_get_label(s_meal_led_green_ret), s_meal_led_green_ret,
        init_get_label(s_meal_led_blue_ret), s_meal_led_blue_ret,
        (long)MEAL_MOTION_TRIGGER_MG, MEAL_MOTION_HOLD_MS, MEAL_REARM_CONFIRM_MS,
        MEAL_MOTION_FILTER_SAMPLES, MEAL_MOTION_WINDOW_MS,
        (long)MEAL_FDC_BASE_PF_X10000,
        (long)MEAL_CONTACT_ON_PF_X10000, (long)MEAL_CONTACT_OFF_PF_X10000,
        MEAL_CONTACT_CONFIRM_MS);

    if (length > 0 && (size_t)length < sizeof(text) &&
        usb_queue_text(text, (size_t)length)) {
        s_should_print_header = false;
    }
}

static int sample_get_ret(const sample_status_t *p_status, uint32_t now_ms,
                          uint32_t stale_ms)
{
    if (p_status->init_ret < 0) {
        return p_status->init_ret;
    }
    if (p_status->sample_ret < 0) {
        return p_status->sample_ret;
    }
    if (!p_status->has_sample) {
        return -ENODATA;
    }
    if (now_ms - p_status->sample_ms > stale_ms) {
        return -ETIMEDOUT;
    }
    return 0;
}

static void value_format(char *p_text, size_t capacity_bytes, bool has_value, int32_t value)
{
    if (has_value) {
        (void)snprintf(p_text, capacity_bytes, "%ld", (long)value);
    } else {
        (void)snprintf(p_text, capacity_bytes, "NA");
    }
}

static void usb_print_sample(uint32_t now_ms)
{
    /* Never add a history of live rows behind an unfinished application TX.
     * Existing bytes remain intact. This cannot clear driver/host-side buffers.
     */
    if (ring_buf_space_get(&s_usb_tx_ring) < USB_TX_BUFFER_BYTES) {
        s_usb_dropped_rows++;
        return;
    }

    char text[USB_ROW_BUFFER_BYTES];
    char button_text[16];
    char battery_text[16];
    char adc_raw_text[16];
    char adc_mv_text[16];
    char x_text[16];
    char y_text[16];
    char z_text[16];
    char fdc_text[24];
    char fdc_delta_text[24];
#if USB_OUTPUT_MEAL_GATES
    char gate_text[32];
#endif
    int acc_ret = sample_get_ret(&s_accel.status, now_ms, ACCEL_STALE_MS);
    int fdc_ret = sample_get_ret(&s_fdc.status, now_ms, FDC_STALE_MS);
    int bat_ret = sample_get_ret(&s_battery_status, now_ms, BATTERY_STALE_MS);

    value_format(button_text, sizeof(button_text), s_button.read_ret == 0,
                 s_button.is_pressed ? 1 : 0);
    value_format(battery_text, sizeof(battery_text), bat_ret == 0, s_battery_mv);
    value_format(adc_raw_text, sizeof(adc_raw_text), bat_ret == 0, s_battery_adc_raw);
    value_format(adc_mv_text, sizeof(adc_mv_text), bat_ret == 0, s_battery_adc_mv);
    value_format(x_text, sizeof(x_text), acc_ret == 0, s_accel.x_mg);
    value_format(y_text, sizeof(y_text), acc_ret == 0, s_accel.y_mg);
    value_format(z_text, sizeof(z_text), acc_ret == 0, s_accel.z_mg);

    /* Still show a clipped reading with fdc_ret=-ERANGE, rather than hide saturation. */
    if (fdc_ret == 0 || (fdc_ret == -ERANGE && s_fdc.status.has_sample &&
        now_ms - s_fdc.status.sample_ms <= FDC_STALE_MS)) {
        value_format_pf(fdc_text, sizeof(fdc_text), s_fdc.capacitance_pf_x10000);
        value_format_pf(fdc_delta_text, sizeof(fdc_delta_text),
                        s_fdc.capacitance_pf_x10000 - MEAL_FDC_BASE_PF_X10000);
    } else {
        (void)snprintf(fdc_text, sizeof(fdc_text), "NA");
        (void)snprintf(fdc_delta_text, sizeof(fdc_delta_text), "NA");
    }
#if USB_OUTPUT_MEAL_GATES
    if (acc_ret == 0 && meal_fdc_is_usable(now_ms)) {
        (void)snprintf(gate_text, sizeof(gate_text), "C=%u M=%u R=%u",
            s_fdc.capacitance_pf_x10000 >= MEAL_CONTACT_ON_PF_X10000 ? 1U : 0U,
            meal_motion_is_recent(now_ms) ? 1U : 0U,
            s_fdc.capacitance_pf_x10000 <= MEAL_CONTACT_OFF_PF_X10000 ? 1U : 0U);
    } else {
        (void)snprintf(gate_text, sizeof(gate_text), "NA");
    }
#endif

    int length = snprintf(text, sizeof(text),
        "[BTN: %s] | [BAT: %s mV] | [ACC: X=%s Y=%s Z=%s mg]"
        " | [FDC: %s pF d=%s pF] | [MOT: %ld mg]"
#if USB_OUTPUT_MEAL_GATES
        " | [GATE: %s]"
#endif
#if USB_OUTPUT_DIAGNOSTICS
        " | [DBG: PWR=%u BTN_RAW=%d HOLD=%lums ADC=%s/%smV"
        " AGE(A/F/B)=%ld/%ld/%ldms MISS=%lu DROP=%lu LED=%d/%d]"
#endif
        , button_text, battery_text, x_text, y_text, z_text, fdc_text, fdc_delta_text,
        (long)s_meal_motion.span_mg
#if USB_OUTPUT_MEAL_GATES
        , gate_text
#endif
#if USB_OUTPUT_DIAGNOSTICS
        , s_is_power_held ? 1U : 0U, s_button.raw_level,
        (unsigned long)s_button.hold_ms, adc_raw_text, adc_mv_text,
        s_accel.status.has_sample ? (long)(now_ms - s_accel.status.sample_ms) : -1L,
        s_fdc.status.has_sample ? (long)(now_ms - s_fdc.status.sample_ms) : -1L,
        s_battery_status.has_sample ? (long)(now_ms - s_battery_status.sample_ms) : -1L,
        (unsigned long)s_missed_ticks, (unsigned long)s_usb_dropped_rows,
        s_led_1_ret, s_led_2_ret < 0 ? s_led_2_ret : (int)atomic_get(&s_led_2_runtime_ret)
#endif
        );

    if (length <= 0 || (size_t)length >= sizeof(text)) {
        s_usb_dropped_rows++;
        return;
    }
    if (s_button.read_ret < 0 || acc_ret < 0 || fdc_ret < 0 || bat_ret < 0) {
        int extra = snprintf(text + length, sizeof(text) - (size_t)length,
            " | [STATUS: BTN=%d BAT=%d ACC=%d FDC=%d]",
            s_button.read_ret, bat_ret, acc_ret, fdc_ret);
        if (extra < 0 || (size_t)extra >= sizeof(text) - (size_t)length) {
            s_usb_dropped_rows++;
            return;
        }
        length += extra;
    }
    int meal_length = snprintf(text + length, sizeof(text) - (size_t)length,
        " | [MEAL: %s] | [COUNT: %lu]", meal_get_pattern(now_ms), (unsigned long)s_meal.count);
    if (meal_length < 0 || (size_t)meal_length >= sizeof(text) - (size_t)length) {
        s_usb_dropped_rows++;
        return;
    }
    length += meal_length;
    if ((size_t)length + 2U >= sizeof(text)) {
        s_usb_dropped_rows++;
        return;
    }
    text[length++] = '\r';
    text[length++] = '\n';
    if (!usb_queue_text(text, (size_t)length)) {
        s_usb_dropped_rows++;
    }
}

/* Process every distinct 20 ms acceleration sample before the FDC state machine. */
static void meal_motion_process(uint32_t now_ms)
{
    if (sample_get_ret(&s_accel.status, now_ms, ACCEL_STALE_MS) != 0) {
        s_meal_motion = (meal_motion_status_t){0};
        return;
    }
    if (s_meal_motion.has_sample && s_meal_motion.sample_ms == s_accel.status.sample_ms) {
        return;
    }
    if (s_meal_motion.has_sample &&
        s_accel.status.sample_ms - s_meal_motion.sample_ms > ACCEL_STALE_MS) {
        s_meal_motion = (meal_motion_status_t){0};
    }
    s_meal_motion.has_sample = true;
    s_meal_motion.sample_ms = s_accel.status.sample_ms;

    int32_t axes[] = { s_accel.x_mg, s_accel.y_mg, s_accel.z_mg };
    uint32_t fi = s_meal_motion.filter_index;
    uint32_t hi = s_meal_motion.history_index;
    if (s_meal_motion.filter_count < MEAL_MOTION_FILTER_SAMPLES) {
        s_meal_motion.filter_count++;
    }
    for (uint32_t axis = 0U; axis < 3U; axis++) {
        s_meal_motion.filter_sum[axis] -= s_meal_motion.filter[fi][axis];
        s_meal_motion.filter[fi][axis] = axes[axis];
        s_meal_motion.filter_sum[axis] += axes[axis];
        s_meal_motion.history[hi][axis] = value_divide_round(
            s_meal_motion.filter_sum[axis], (int32_t)s_meal_motion.filter_count);
    }
    s_meal_motion.filter_index = (fi + 1U) % MEAL_MOTION_FILTER_SAMPLES;
    s_meal_motion.history_ms[hi] = s_accel.status.sample_ms;
    s_meal_motion.history_index = (hi + 1U) % MEAL_MOTION_HISTORY_SAMPLES;
    if (s_meal_motion.history_count < MEAL_MOTION_HISTORY_SAMPLES) {
        s_meal_motion.history_count++;
    }

    int32_t low[3] = { INT32_MAX, INT32_MAX, INT32_MAX };
    int32_t high[3] = { INT32_MIN, INT32_MIN, INT32_MIN };
    uint32_t used = 0U;
    for (uint32_t i = 0U; i < s_meal_motion.history_count; i++) {
        uint32_t age_ms = s_accel.status.sample_ms - s_meal_motion.history_ms[i];
        if (age_ms > MEAL_MOTION_WINDOW_MS) {
            continue;
        }
        used++;
        for (uint32_t axis = 0U; axis < 3U; axis++) {
            int32_t value = s_meal_motion.history[i][axis];
            if (value < low[axis]) {
                low[axis] = value;
            }
            if (value > high[axis]) {
                high[axis] = value;
            }
        }
    }
    int64_t span = 0;
    for (uint32_t axis = 0U; axis < 3U; axis++) {
        int64_t axis_span = (int64_t)high[axis] - low[axis];
        if (axis_span > span) {
            span = axis_span;
        }
    }
    s_meal_motion.span_mg = span;
    if (used >= MEAL_MOTION_MIN_SAMPLES && span >= MEAL_MOTION_TRIGGER_MG) {
        s_meal_motion.has_motion = true;
        s_meal_motion.motion_ms = s_accel.status.sample_ms;
    }
}

static bool meal_motion_is_recent(uint32_t now_ms)
{
    return sample_get_ret(&s_accel.status, now_ms, ACCEL_STALE_MS) == 0 &&
        s_meal_motion.has_motion && now_ms - s_meal_motion.motion_ms <= MEAL_MOTION_HOLD_MS;
}

static const char *meal_get_pattern(uint32_t now_ms)
{
    switch (s_meal.state) {
    case MEAL_STATE_READY:
        return meal_motion_is_recent(now_ms) ? "MOVE" : "READY";
    case MEAL_STATE_EATING:
        return "EATING";
    case MEAL_STATE_RETURN:
        return "RETURN";
    case MEAL_STATE_ERROR:
        return "ERROR";
    default:
        return "WAIT_READY";
    }
}

static void meal_update_leds(void)
{
    bool is_eating = s_meal.state == MEAL_STATE_EATING;
    /* gpio_pin_set_dt uses logical ON; the overlay handles active-low outputs. */
    if (s_meal_led_green_ret == 0) {
        s_meal_led_green_ret = gpio_pin_set_dt(&s_meal_led_green, is_eating ? 1 : 0);
    }
    if (s_meal_led_blue_ret == 0) {
        s_meal_led_blue_ret = gpio_pin_set_dt(&s_meal_led_blue, is_eating ? 0 : 1);
    }
}

static void meal_process(uint32_t now_ms)
{
    int acc_ret = sample_get_ret(&s_accel.status, now_ms, ACCEL_STALE_MS);
    if (!s_is_power_held || acc_ret != 0 || !meal_fdc_is_usable(now_ms)) {
        s_meal.state = s_is_power_held && s_has_sensor_init_started ?
            MEAL_STATE_ERROR : MEAL_STATE_WAIT_READY;
        s_meal.is_confirmation_pending = false;
        s_meal.has_fdc_sample = false;
        s_meal_motion.has_motion = false;
        meal_update_leds();
        return;
    }
    if (s_meal.state == MEAL_STATE_ERROR) {
        /* After recovery, require release rearm before another bite. */
        s_meal.state = MEAL_STATE_WAIT_READY;
    }
    /* Evaluate only new FDC measurements, not repeated USB rows or timer ticks. */
    if (s_meal.has_fdc_sample && s_meal.last_fdc_sample_ms == s_fdc.status.sample_ms) {
        return;
    }
    s_meal.has_fdc_sample = true;
    s_meal.last_fdc_sample_ms = s_fdc.status.sample_ms;

    bool is_contact = s_fdc.capacitance_pf_x10000 >= MEAL_CONTACT_ON_PF_X10000;
    bool is_released = s_fdc.capacitance_pf_x10000 <= MEAL_CONTACT_OFF_PF_X10000;
    bool is_condition;
    uint32_t confirm_ms;

    if (s_meal.state == MEAL_STATE_EATING) {
        is_condition = is_released;
        confirm_ms = MEAL_RELEASE_CONFIRM_MS;
    } else if (s_meal.state == MEAL_STATE_READY) {
        is_condition = is_contact && meal_motion_is_recent(now_ms);
        confirm_ms = MEAL_CONTACT_CONFIRM_MS;
    } else {
        is_condition = is_released;
        confirm_ms = MEAL_REARM_CONFIRM_MS;
    }

    if (!is_condition) {
        s_meal.is_confirmation_pending = false;
    } else if (!s_meal.is_confirmation_pending) {
        s_meal.is_confirmation_pending = true;
        s_meal.confirmation_since_ms = now_ms;
    } else if (now_ms - s_meal.confirmation_since_ms >= confirm_ms) {
        s_meal.is_confirmation_pending = false;
        if (s_meal.state == MEAL_STATE_READY) {
            s_meal.state = MEAL_STATE_EATING;
            s_meal.count++;
        } else if (s_meal.state == MEAL_STATE_EATING) {
            s_meal.state = MEAL_STATE_RETURN;
        } else {
            s_meal.state = MEAL_STATE_READY;
            /* Start a fresh motion window for the next approach, in the current hand pose. */
            s_meal_motion = (meal_motion_status_t){0};
        }
    }
    meal_update_leds();
}

static bool meal_fdc_is_usable(uint32_t now_ms)
{
    int ret = sample_get_ret(&s_fdc.status, now_ms, FDC_STALE_MS);
    bool is_positive_clipped = ret == -ERANGE && s_fdc.status.init_ret == 0 &&
        s_fdc.status.has_sample && now_ms - s_fdc.status.sample_ms <= FDC_STALE_MS &&
        s_fdc.capacitance_pf_x10000 >= MEAL_CONTACT_ON_PF_X10000;
    return ret == 0 || is_positive_clipped;
}

static void value_format_pf(char *p_text, size_t capacity_bytes, int32_t value)
{
    uint32_t magnitude = (uint32_t)((value < 0) ? -(int64_t)value : value);
    (void)snprintf(p_text, capacity_bytes, "%s%lu.%04lu", value < 0 ? "-" : "",
        (unsigned long)(magnitude / 10000U), (unsigned long)(magnitude % 10000U));
}
