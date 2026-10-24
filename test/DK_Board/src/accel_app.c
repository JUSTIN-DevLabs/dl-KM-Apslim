/**
 * @file accel_app.c
 * @brief Non-blocking test scheduler, I2C adapter, USB output, and heartbeat LED.
 */
#include "accel_app.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "lis2dh.h"

#define ACCEL_SAMPLE_PERIOD_MS       20U
#define ACCEL_LED_TOGGLE_PERIOD_MS   500U
#define ACCEL_ERROR_REPORT_INTERVAL 10U
/* Use board-defined node labels instead of custom aliases. */
#define ACCEL_I2C_NODE              DT_NODELABEL(i2c0)
#define ACCEL_LED_NODE              DT_NODELABEL(led0)
#define ACCEL_USB_NODE              DT_NODELABEL(cdc_acm_uart0)

/* Fail early if the required overlay or board target is not applied. */
#if !DT_NODE_HAS_STATUS(ACCEL_I2C_NODE, okay)
#error "I2C0 disabled: app.overlay missing or incorrect board selected"
#endif

#if !DT_NODE_EXISTS(ACCEL_LED_NODE)
#error "Missing led0 board node: select raytac_mdbt50q_db_40/nrf52840"
#endif

#if !DT_NODE_HAS_STATUS(ACCEL_USB_NODE, okay)
#error "Missing USB CDC node: ensure DK_Board/app.overlay is applied"
#endif

static const struct device *s_p_i2c_device = DEVICE_DT_GET(ACCEL_I2C_NODE);
static const struct device *s_p_usb_console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct gpio_dt_spec s_led = GPIO_DT_SPEC_GET(ACCEL_LED_NODE, gpios);

static lis2dh_device_t s_lis2dh_device;
static bool s_is_sensor_ready;
static bool s_was_usb_connected;
static uint32_t s_error_report_count;
static struct k_timer s_led_timer;
static struct k_timer s_sensor_timer;
static struct k_work s_led_work;
static struct k_work s_sensor_work;

static int accel_app_bus_read(void *p_context, uint8_t i2c_address,
                              uint8_t register_address, uint8_t *p_data,
                              size_t size_bytes);
static int accel_app_bus_write(void *p_context, uint8_t i2c_address,
                               uint8_t register_address, uint8_t value);
static bool accel_app_is_usb_connected(void);
static void accel_app_led_timer_handler(struct k_timer *p_timer);
static void accel_app_sensor_timer_handler(struct k_timer *p_timer);
static void accel_app_led_work_handler(struct k_work *p_work);
static void accel_app_sensor_work_handler(struct k_work *p_work);

int accel_app_init(void)
{
    if (!device_is_ready(s_led.port) || !device_is_ready(s_p_i2c_device)) {
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&s_led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    k_work_init(&s_led_work, accel_app_led_work_handler);
    k_work_init(&s_sensor_work, accel_app_sensor_work_handler);
    k_timer_init(&s_led_timer, accel_app_led_timer_handler, NULL);
    k_timer_init(&s_sensor_timer, accel_app_sensor_timer_handler, NULL);

    /* Timer callbacks only enqueue work; GPIO, I2C, and USB run in thread context. */
    k_timer_start(&s_led_timer, K_MSEC(ACCEL_LED_TOGGLE_PERIOD_MS),
                  K_MSEC(ACCEL_LED_TOGGLE_PERIOD_MS));
    k_timer_start(&s_sensor_timer, K_MSEC(ACCEL_SAMPLE_PERIOD_MS),
                  K_MSEC(ACCEL_SAMPLE_PERIOD_MS));

    return 0;
}

static int accel_app_bus_read(void *p_context, uint8_t i2c_address,
                              uint8_t register_address, uint8_t *p_data,
                              size_t size_bytes)
{
    if ((p_context == NULL) || (p_data == NULL) || (size_bytes == 0U)) {
        return -EINVAL;
    }

    const struct device *p_bus = p_context;

    return i2c_write_read(p_bus, i2c_address, &register_address,
                          sizeof(register_address), p_data, size_bytes);
}

static int accel_app_bus_write(void *p_context, uint8_t i2c_address,
                               uint8_t register_address, uint8_t value)
{
    if (p_context == NULL) {
        return -EINVAL;
    }

    const struct device *p_bus = p_context;
    return i2c_reg_write_byte(p_bus, i2c_address, register_address, value);
}

static bool accel_app_is_usb_connected(void)
{
    if (!device_is_ready(s_p_usb_console)) {
        return false;
    }

    uint32_t dtr = 0U;
    int ret = uart_line_ctrl_get(s_p_usb_console, UART_LINE_CTRL_DTR, &dtr);

    return (ret == 0) && (dtr != 0U);
}

static void accel_app_led_timer_handler(struct k_timer *p_timer)
{
    ARG_UNUSED(p_timer);
    (void)k_work_submit(&s_led_work);
}

static void accel_app_sensor_timer_handler(struct k_timer *p_timer)
{
    ARG_UNUSED(p_timer);
    (void)k_work_submit(&s_sensor_work);
}

static void accel_app_led_work_handler(struct k_work *p_work)
{
    ARG_UNUSED(p_work);
    (void)gpio_pin_toggle_dt(&s_led);
}

static void accel_app_sensor_work_handler(struct k_work *p_work)
{
    ARG_UNUSED(p_work);

    bool is_usb_connected = accel_app_is_usb_connected();
    if (is_usb_connected && !s_was_usb_connected) {
        printk("\r\n[LIS2DH] DK test started\r\n");
        printk("[LIS2DH] I2C SCL=P0.08, SDA=P0.06; LED=P0.13\r\n");
        printk("[LIS2DH] Sensor=100Hz/2g/HR; Output=100ms\r\n");
        if (s_is_sensor_ready) {
            printk("[LIS2DH] Ready at 0x%02X (WHO_AM_I=0x33)\r\n",
                   s_lis2dh_device.i2c_address);
        }
    }
    s_was_usb_connected = is_usb_connected;

    if (!s_is_sensor_ready) {
        const lis2dh_bus_t bus = {
            .read = accel_app_bus_read,
            .write = accel_app_bus_write,
            .p_context = (void *)s_p_i2c_device,
        };

        int ret = lis2dh_init(&s_lis2dh_device, &bus);
        if (ret < 0) {
            s_error_report_count++;
            if (is_usb_connected &&
                (s_error_report_count % ACCEL_ERROR_REPORT_INTERVAL == 0U)) {
                printk("[LIS2DH] Probe failed (%d); check wiring/SA0\r\n", ret);
            }
            return;
        }

        s_is_sensor_ready = true;
        s_error_report_count = 0U;
        if (is_usb_connected) {
            printk("[LIS2DH] Ready at 0x%02X (WHO_AM_I=0x33)\r\n",
                   s_lis2dh_device.i2c_address);
        }
    }

    lis2dh_sample_t sample = {0};
    int ret = lis2dh_read_accel(&s_lis2dh_device, &sample);
    if (ret < 0) {
        s_error_report_count++;
        if (is_usb_connected &&
            (s_error_report_count % ACCEL_ERROR_REPORT_INTERVAL == 0U)) {
            printk("[LIS2DH] Read failed (%d)\r\n", ret);
        }
        return;
    }

    s_error_report_count = 0U;
    if (is_usb_connected) {
        printk("[%u ms] X=%d mg, Y=%d mg, Z=%d mg\r\n",
               k_uptime_get_32(), sample.x_mg, sample.y_mg, sample.z_mg);
    }
}
