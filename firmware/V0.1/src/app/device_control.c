#include "device_control.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "accel_detect.h"
#include "accel_service.h"
#include "app_config.h"
#include "battery_service.h"
#include "button_service.h"
#include "capacitive_detect.h"
#include "capacitive_service.h"
#include "i2c_hal.h"
#include "led_hal.h"
#include "power_service.h"
#include "pwr_hal.h"
#include "usb_serial.h"

typedef struct {
    power_service_t power;
    button_service_t button;
    accel_service_t accel;
    capacitive_service_t cap;
    battery_service_t battery;
    accel_detect_t motion;
    meal_pattern_t meal;
    capacitive_detect_result_t cap_result;
    uint32_t last_accel_ms;
    uint32_t last_cap_ms;
    uint32_t last_battery_ms;
    uint32_t last_usb_ms;
    uint32_t debug_since_ms;
    uint32_t green_since_ms;
    uint32_t cap_gap_since_ms;
    uint32_t missed_ticks;
    uint32_t dropped_rows;
    bool is_debug_on;
    bool is_green_on;
    bool is_started;
    bool is_vbus_present;
    bool should_print_header;
    bool has_cap_ready;
    bool has_cap_gap;
    int raw_button;
    int usb_ret;
} device_state_t;

static device_state_t s_device;
static const capacitive_detect_config_t s_cap_config = {
    .baseline_pf_x10000 = APP_MEAL_FDC_BASE_PF_X10000,
    .on_delta_pf_x10000 = APP_MEAL_CONTACT_ON_DELTA,
    .off_delta_pf_x10000 = APP_MEAL_CONTACT_OFF_DELTA,
};
static const accel_detect_config_t s_motion_config = {
    .window_ms = APP_MEAL_MOTION_WINDOW_MS,
    .hold_ms = APP_MEAL_MOTION_HOLD_MS,
    .threshold_mg = APP_MEAL_MOTION_TRIGGER_MG,
    .min_samples = APP_MEAL_MOTION_MIN_SAMPLES,
};
static const meal_pattern_config_t s_meal_config = {
    .contact_ms = APP_MEAL_CONTACT_CONFIRM_MS,
    .release_ms = APP_MEAL_RELEASE_CONFIRM_MS,
    .rearm_ms = APP_MEAL_REARM_CONFIRM_MS,
};

/* Private function prototypes */
static bool device_is_due(uint32_t now_ms, uint32_t *last_ms, uint32_t period_ms);
static void device_sensors_start(uint32_t now_ms);
static void device_sensors_stop(void);
static void device_update_meal(uint32_t now_ms, bool has_new_accel, bool has_new_cap);
static void device_update_leds(uint32_t now_ms);
static void device_format_value(char *text, size_t length, bool usable, int32_t value);
static void device_format_pf(char *text, size_t length, bool usable, int32_t value);
static void device_print_header(void);
static void device_print_sample(uint32_t now_ms);

/* Public functions */
int device_control_init(void)
{
    memset(&s_device, 0, sizeof(s_device));
    (void)led_init();
    int ret = power_service_init(&s_device.power);
    if (ret < 0) {
        return ret;
    }
    button_service_init(&s_device.button, APP_BUTTON_DEBOUNCE_MS, APP_POWER_LONG_PRESS_MS);
    accel_detect_init(&s_device.motion, &s_motion_config);
    meal_pattern_init(&s_device.meal, &s_meal_config);
    s_device.accel.status.init_ret = -EINPROGRESS;
    s_device.cap.status.init_ret = -EINPROGRESS;
    s_device.battery.status.init_ret = -EINPROGRESS;
    s_device.usb_ret = usb_serial_init();
    s_device.should_print_header = true;
    s_device.is_vbus_present = pwr_vbus_is_present();
    device_update_leds(0U);
    return 0;
}

void device_control_process(uint32_t now_ms, uint32_t elapsed_ticks)
{
    if (elapsed_ticks > 1U) {
        s_device.missed_ticks += elapsed_ticks - 1U;
        /* Unsampled time cannot prove continuous button input for 3 seconds. */
        (void)button_service_update(&s_device.button, now_ms, -EAGAIN, false);
    }
    int level = 0;
    int ret = pwr_sw_state_read_raw(&level);
    s_device.raw_button = level;
    bool has_long_press = button_service_update(&s_device.button, now_ms, ret,
                                                ret == 0 && pwr_sw_level_is_pressed(level));
    if (power_service_update(&s_device.power, has_long_press)) {
        if (s_device.power.is_on) {
            device_sensors_start(now_ms);
        } else {
            device_sensors_stop();
        }
    }
    s_device.is_vbus_present = pwr_vbus_is_present();
    if (usb_serial_process()) {
        s_device.should_print_header = true;
    }

    bool has_new_accel = false;
    bool has_new_cap = false;
    uint32_t previous_raw_cap_sequence = s_device.cap.driver.raw_sequence;
    if (s_device.is_started) {
        bool accel_due = device_is_due(now_ms, &s_device.last_accel_ms, APP_ACCEL_SAMPLE_PERIOD_MS);
        bool cap_due = device_is_due(now_ms, &s_device.last_cap_ms, APP_FDC_SAMPLE_PERIOD_MS);
        has_new_accel = accel_service_process(&s_device.accel, now_ms, accel_due);
        has_new_cap = capacitive_service_process(&s_device.cap, now_ms, cap_due);
        if (device_is_due(now_ms, &s_device.last_battery_ms, APP_BATTERY_SAMPLE_PERIOD_MS)) {
            battery_service_process(&s_device.battery, now_ms);
        }
        device_update_meal(now_ms, has_new_accel, has_new_cap);
    }
    device_update_leds(now_ms);
    bool output_due = device_is_due(now_ms, &s_device.last_usb_ms, APP_USB_OUTPUT_PERIOD_MS);
    if (usb_serial_is_connected()) {
        if (s_device.should_print_header) {
            device_print_header();
        }
        if (!s_device.should_print_header &&
            ((APP_USB_LOG_EVERY_SAMPLE != 0U &&
              (has_new_accel || s_device.cap.driver.raw_sequence != previous_raw_cap_sequence)) ||
             (APP_USB_LOG_EVERY_SAMPLE == 0U && output_due))) {
            device_print_sample(now_ms);
        }
    }
}

bool device_control_is_on(void)
{
    return s_device.power.is_on;
}

uint32_t device_control_meal_count(void)
{
    return s_device.meal.count;
}

meal_state_t device_control_meal_state(void)
{
    return s_device.meal.state;
}

/* Private functions */
static bool device_is_due(uint32_t now_ms, uint32_t *last_ms, uint32_t period_ms)
{
    uint32_t elapsed_ms = now_ms - *last_ms;
    if (elapsed_ms < period_ms) {
        return false;
    }
    *last_ms += (elapsed_ms / period_ms) * period_ms;
    /* Sample once at the actual current time, never invent missed past samples. */
    return true;
}

static void device_sensors_start(uint32_t now_ms)
{
    const fdc1004_config_t fdc_config = {
        .initial_capdac = APP_FDC_INITIAL_CAPDAC,
        .capdac_step = APP_FDC_CAPDAC_STEP,
        .is_auto_range_enabled = APP_FDC_AUTO_RANGE != 0U,
        .timeout_ms = APP_FDC_CONVERSION_TIMEOUT_MS,
    };
    (void)accel_service_init(&s_device.accel, i2c_hal_accel_bus(), now_ms, APP_ACCEL_MA_SAMPLES);
    (void)capacitive_service_init(&s_device.cap, i2c_hal_capacitive_bus(), &fdc_config,
                                  APP_FDC_MA_SAMPLES);
    (void)battery_service_init(&s_device.battery);
    accel_detect_init(&s_device.motion, &s_motion_config);
    meal_pattern_init(&s_device.meal, &s_meal_config);
    s_device.last_accel_ms = now_ms - APP_ACCEL_SAMPLE_PERIOD_MS;
    s_device.last_cap_ms = now_ms - APP_FDC_SAMPLE_PERIOD_MS;
    s_device.last_battery_ms = now_ms - APP_BATTERY_SAMPLE_PERIOD_MS;
    s_device.debug_since_ms = now_ms;
    s_device.is_green_on = false;
    s_device.is_debug_on = false;
    s_device.has_cap_gap = false;
    s_device.has_cap_ready = false;
    s_device.is_started = true;
    s_device.should_print_header = true;
}

static void device_sensors_stop(void)
{
    if (s_device.is_started) {
        accel_service_stop(&s_device.accel);
        capacitive_service_stop(&s_device.cap);
    }
    s_device.is_started = false;
    s_device.is_green_on = false;
    s_device.is_debug_on = false;
    s_device.meal.state = MEAL_STATE_WAIT_READY;
    s_device.should_print_header = true;
}

static void device_update_meal(uint32_t now_ms, bool has_new_accel, bool has_new_cap)
{
    bool is_accel_usable =
        sensor_status_check(&s_device.accel.status, now_ms, APP_ACCEL_STALE_MS) == 0;
    bool is_cap_usable = sensor_status_check(&s_device.cap.status, now_ms, APP_FDC_STALE_MS) == 0;
    if (has_new_accel && is_accel_usable) {
        accel_detect_update(&s_device.motion, s_device.accel.status.sample_ms,
                            s_device.accel.filtered_mg);
    }
    if (is_cap_usable) {
        s_device.has_cap_ready = true;
        s_device.has_cap_gap = false;
    } else if (s_device.has_cap_ready &&
               (s_device.cap.driver.is_ranging ||
                (s_device.cap.status.sample_ret == 0 && !s_device.cap.status.is_filter_ready))) {
        if (!s_device.has_cap_gap) {
            s_device.has_cap_gap = true;
            s_device.cap_gap_since_ms = now_ms;
        }
        if (is_accel_usable && now_ms - s_device.cap_gap_since_ms <= APP_FDC_STALE_MS) {
            meal_pattern_pause(&s_device.meal);
            return;
        }
    }
    if (!is_accel_usable || !is_cap_usable) {
        meal_pattern_invalidate(&s_device.meal);
        accel_detect_reset(&s_device.motion);
        if (s_device.cap.status.sample_ret != -EAGAIN) {
            s_device.has_cap_ready = false;
        }
        return;
    }
    if (!has_new_cap) {
        return;
    }
    s_device.cap_result =
        capacitive_detect_evaluate(&s_cap_config, s_device.cap.filtered_pf_x10000);
    bool recent = accel_detect_is_recent(&s_device.motion, now_ms);
    if (meal_pattern_update(&s_device.meal, s_device.cap.status.sample_ms, s_device.cap_result,
                            recent)) {
        s_device.is_green_on = true;
        s_device.green_since_ms = now_ms;
    }
    if (s_device.meal.has_become_ready) {
        accel_detect_reset(&s_device.motion);
    }
}

static void device_update_leds(uint32_t now_ms)
{
    if (s_device.power.is_on) {
        uint32_t toggles = (now_ms - s_device.debug_since_ms) / APP_DEBUG_LED_1_TOGGLE_MS;
        if (toggles != 0U) {
            s_device.debug_since_ms += toggles * APP_DEBUG_LED_1_TOGGLE_MS;
            if ((toggles & 1U) != 0U) {
                s_device.is_debug_on = !s_device.is_debug_on;
            }
        }
        if (s_device.is_green_on && now_ms - s_device.green_since_ms >= APP_MEAL_GREEN_PULSE_MS) {
            s_device.is_green_on = false;
        }
    } else {
        s_device.is_debug_on = false;
        s_device.is_green_on = false;
    }
    (void)led_set(LED_RED, s_device.is_vbus_present);
    (void)led_set(LED_GREEN, s_device.power.is_on && s_device.is_green_on);
    (void)led_set(LED_BLUE, s_device.power.is_on && !s_device.is_green_on);
    (void)led_set(LED_DEBUG_1, s_device.is_debug_on);
    (void)led_set(LED_DEBUG_2, s_device.button.read_ret == 0 && s_device.button.is_pressed);
}

static void device_format_value(char *text, size_t length, bool usable, int32_t value)
{
    if (usable) {
        (void)snprintf(text, length, "%ld", (long)value);
    } else {
        (void)snprintf(text, length, "NA");
    }
}

static void device_format_pf(char *text, size_t length, bool usable, int32_t value)
{
    if (!usable) {
        (void)snprintf(text, length, "NA");
        return;
    }
    uint32_t magnitude = (uint32_t)(value < 0 ? -(int64_t)value : value);
    (void)snprintf(text, length, "%s%lu.%04lu", value < 0 ? "-" : "",
                   (unsigned long)(magnitude / 10000U), (unsigned long)(magnitude % 10000U));
}

/* [시리얼 시작 안내문 수정 위치] 전원 ON 후 한 번 출력하는 설정 안내입니다. */
static void device_print_header(void)
{
    char text[1600];
    int length = snprintf(
        text, sizeof(text),
        "\r\n============================================================\r\n"
        "APSLIM v0.1 MODULAR | TIMER2 | 20261024\r\n"
        "RGB active LOW: R=P1.08, G=P0.11 pulse=%u ms, B=P0.12 idle\r\n"
        "DEBUG1 P0.31: %u ms toggle while ON; DEBUG2 P0.30: pressed\r\n"
        "TIMER2 tick=%u ms; ACC=%u ms FDC=%u ms USB=%u ms\r\n"
        "MA ACC=%u FDC=%u (1=bypass, max=32); warm-up must finish\r\n"
        "ACC: %s 7bit=0x%02X WHO=0x%02X INIT=%d\r\n"
        "FDC: CIN1 0x50 INIT=%d AUTORANGE=%u CAPDAC=%u; clipped values NOT counted\r\n"
        "MOTION threshold=%ld mg window=%u ms hold=%u ms (not angle/height)\r\n"
        "FDC baseline=%ld/10000 pF delta ON=%ld OFF=%ld; confirm=%u/%u rearm=%u ms\r\n"
        "USB=%d LOG_ALL=%u; compact sensor log\r\n"
        "ACC/FDC=filtered, *_RAW=unfiltered; *_VALID and STATUS mark quality\r\n"
        "USB snapshot mode may omit intermediate samples; LOG_ALL attempts every new sample\r\n"
        "Meal count is a provisional contact+motion estimate.\r\n"
        "============================================================\r\n",
        APP_MEAL_GREEN_PULSE_MS, APP_DEBUG_LED_1_TOGGLE_MS, APP_TIMER_TICK_MS,
        APP_ACCEL_SAMPLE_PERIOD_MS, APP_FDC_SAMPLE_PERIOD_MS, APP_USB_OUTPUT_PERIOD_MS,
        APP_ACCEL_MA_SAMPLES, APP_FDC_MA_SAMPLES, lis2dh12_name(&s_device.accel.driver),
        s_device.accel.driver.address, s_device.accel.driver.who_am_i,
        s_device.accel.status.init_ret, s_device.cap.status.init_ret, APP_FDC_AUTO_RANGE,
        s_device.cap.driver.capdac, (long)APP_MEAL_MOTION_TRIGGER_MG, APP_MEAL_MOTION_WINDOW_MS,
        APP_MEAL_MOTION_HOLD_MS, (long)APP_MEAL_FDC_BASE_PF_X10000, (long)APP_MEAL_CONTACT_ON_DELTA,
        (long)APP_MEAL_CONTACT_OFF_DELTA, APP_MEAL_CONTACT_CONFIRM_MS, APP_MEAL_RELEASE_CONFIRM_MS,
        APP_MEAL_REARM_CONFIRM_MS, s_device.usb_ret, APP_USB_LOG_EVERY_SAMPLE);
    if (length > 0 && (size_t)length < sizeof(text) && usb_serial_write(text, (size_t)length)) {
        s_device.should_print_header = false;
    }
}

/* [시리얼 주기 출력 수정 위치]
 * snprintf의 문자열과 아래 인자를 같은 순서로 함께 수정합니다.
 * 취득 순번/시각은 내부 판정에 유지하고, USB 출력에서만 생략합니다.
 */
static void device_print_sample(uint32_t now_ms)
{
    char text[1024];
    char axes[3][16];
    char raw_axes[3][16];
    char cap[24];
    char raw_cap[24];
    char delta[24];
    int ar = sensor_status_check(&s_device.accel.status, now_ms, APP_ACCEL_STALE_MS);
    int fr = sensor_status_check(&s_device.cap.status, now_ms, APP_FDC_STALE_MS);
    for (uint8_t i = 0U; i < 3U; i++) {
        device_format_value(axes[i], sizeof(axes[i]), ar == 0, s_device.accel.filtered_mg[i]);
        device_format_value(raw_axes[i], sizeof(raw_axes[i]), s_device.accel.status.has_sample,
                            s_device.accel.raw_mg[i]);
    }
    device_format_pf(cap, sizeof(cap), fr == 0, s_device.cap.filtered_pf_x10000);
    device_format_pf(delta, sizeof(delta), fr == 0,
                     s_device.cap.filtered_pf_x10000 - APP_MEAL_FDC_BASE_PF_X10000);
    device_format_pf(raw_cap, sizeof(raw_cap), s_device.cap.driver.raw_sequence > 0U,
                     s_device.cap.driver.raw_pf_x10000);
    int length = snprintf(
        text, sizeof(text),
        "[ACC: X=%s Y=%s Z=%s mg]"
        " | [FDC: %s pF d=%s pF]"
        " | [ACC_RAW: X=%s Y=%s Z=%s mg]"
        " | [A: VALID=%u]"
        " | [FDC_RAW: %s pF CODE=%ld CAPDAC=%u RAW_VALID=%u]"
        " | [F: VALID=%u RANGE=%u]"
        " | [MOT: %ld mg] | [MEAL: %s] | [COUNT: %lu]"
        " | [STATUS: ACC=%d FDC=%d]"
        " | | [MISS: %lu USB_DROP: %lu]\r\n",
        axes[0], axes[1], axes[2], cap, delta, raw_axes[0], raw_axes[1], raw_axes[2],
        ar == 0 ? 1U : 0U, raw_cap, (long)s_device.cap.driver.raw_code,
        s_device.cap.driver.raw_capdac, s_device.cap.driver.is_raw_valid ? 1U : 0U,
        fr == 0 ? 1U : 0U, s_device.cap.driver.is_ranging ? 1U : 0U, (long)s_device.motion.span_mg,
        meal_pattern_name(&s_device.meal,
                          ar == 0 && accel_detect_is_recent(&s_device.motion, now_ms)),
        (unsigned long)s_device.meal.count, ar, fr, led_get_status(LED_RED),
        led_get_status(LED_GREEN), led_get_status(LED_BLUE), led_get_status(LED_DEBUG_1),
        led_get_status(LED_DEBUG_2), (unsigned long)s_device.missed_ticks,
        (unsigned long)s_device.dropped_rows);
    if (length <= 0 || (size_t)length >= sizeof(text) || !usb_serial_write(text, (size_t)length)) {
        s_device.dropped_rows++;
    }
}
