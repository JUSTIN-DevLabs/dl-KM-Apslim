/*
 * APSLIM Rev.A - Hardware Bring-up
 * MCU: nRF52840 (Raytac MDBT50Q)
 * SDK: nRF Connect SDK v3.4.1 / Zephyr (legacy USB device stack)
 *
 * Kept: LED(P0.31), BTN(P0.26), BAT ADC(P0.02), USB CDC.
 * Updated: LIS2DTW12 WHO_AM_I=0x44, repeated I2C0/I2C1 checks.
 *
 * app.overlay must provide:
 *   led0, sw0, zephyr,user/battery_adc, i2c0, i2c1, usb_cdc.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/usb/usb_device.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

/* =========================================================
 * Adjustable periods
 * ========================================================= */
#define LED_TOGGLE_PERIOD_MS     500
#define ADC_SAMPLE_PERIOD_MS     500
#define BUTTON_DEBOUNCE_MS        30
#define I2C_POLL_PERIOD_MS        20
#define I2C_REPORT_PERIOD_MS    1000
#define MAIN_LOOP_PERIOD_MS       10

/* =========================================================
 * DeviceTree handles (same interface as the original main.c)
 * ========================================================= */
static const struct gpio_dt_spec debug_led1 =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static const struct adc_dt_spec battery_adc =
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), battery_adc);

static const struct device *const lis_i2c =
    DEVICE_DT_GET(DT_NODELABEL(i2c0));

static const struct device *const fdc_i2c =
    DEVICE_DT_GET(DT_NODELABEL(i2c1));

static const struct device *const cdc_dev =
    DEVICE_DT_GET(DT_NODELABEL(usb_cdc));

/* =========================================================
 * I2C sensor IDs / registers
 * ========================================================= */
#define LIS2DTW12_ADDR_LOW        0x18
#define LIS2DTW12_ADDR_HIGH       0x19
#define LIS2DTW12_WHO_AM_I_REG    0x0F
#define LIS2DTW12_WHO_AM_I_VALUE  0x44

#define FDC1004_ADDR              0x50
#define FDC1004_DEVICE_ID_REG     0xFF
#define FDC1004_DEVICE_ID_VALUE   0x1004

struct i2c_probe {
    int last_ret;
    uint16_t last_id;
    uint32_t attempts;
    uint32_t reads_ok;
    uint32_t id_match;
};

static void usb_send_string(const char *str)
{
    while (*str != '\0') {
        uart_poll_out(cdc_dev, (unsigned char)*str);
        str++;
    }
}

static void probe_lis2dtw12(uint8_t addr, struct i2c_probe *status)
{
    uint8_t id = 0;
    int ret;

    if (!device_is_ready(lis_i2c)) {
        ret = -ENODEV;
    } else {
        ret = i2c_reg_read_byte(lis_i2c, addr,
                                LIS2DTW12_WHO_AM_I_REG, &id);
    }

    status->attempts++;
    status->last_ret = ret;
    status->last_id = (ret == 0) ? id : 0;

    if (ret == 0) {
        status->reads_ok++;
        if (id == LIS2DTW12_WHO_AM_I_VALUE) {
            status->id_match++;
        }
    }
}

static void probe_fdc1004(struct i2c_probe *status)
{
    const uint8_t reg = FDC1004_DEVICE_ID_REG;
    uint8_t rx[2] = {0, 0};
    uint16_t id = 0;
    int ret;

    if (!device_is_ready(fdc_i2c)) {
        ret = -ENODEV;
    } else {
        ret = i2c_write_read(fdc_i2c, FDC1004_ADDR,
                             &reg, sizeof(reg), rx, sizeof(rx));
    }

    if (ret == 0) {
        id = ((uint16_t)rx[0] << 8) | rx[1];
    }

    status->attempts++;
    status->last_ret = ret;
    status->last_id = id;

    if (ret == 0) {
        status->reads_ok++;
        if (id == FDC1004_DEVICE_ID_VALUE) {
            status->id_match++;
        }
    }
}

static void print_probe_status(const char *label,
                               const struct i2c_probe *status,
                               bool id_is_16bit)
{
    char buffer[144];

    if (status->last_ret == 0) {
        if (id_is_16bit) {
            snprintf(buffer, sizeof(buffer),
                     "%s: ret=0 ID=0x%04X | READ=%lu/%lu MATCH=%lu\r\n",
                     label, (unsigned int)status->last_id,
                     (unsigned long)status->reads_ok,
                     (unsigned long)status->attempts,
                     (unsigned long)status->id_match);
        } else {
            snprintf(buffer, sizeof(buffer),
                     "%s: ret=0 WHO=0x%02X | READ=%lu/%lu MATCH=%lu\r\n",
                     label, (unsigned int)status->last_id,
                     (unsigned long)status->reads_ok,
                     (unsigned long)status->attempts,
                     (unsigned long)status->id_match);
        }
    } else {
        snprintf(buffer, sizeof(buffer),
                 "%s: ret=%d READ_FAIL | READ=%lu/%lu MATCH=%lu\r\n",
                 label, status->last_ret,
                 (unsigned long)status->reads_ok,
                 (unsigned long)status->attempts,
                 (unsigned long)status->id_match);
    }
    usb_send_string(buffer);
}

static void print_i2c_status(const struct i2c_probe *lis18,
                             const struct i2c_probe *lis19,
                             const struct i2c_probe *fdc)
{
    usb_send_string("\r\n----------- I2C CHECK -----------\r\n");
    usb_send_string(device_is_ready(lis_i2c) ?
                    "I2C0 (LIS): READY\r\n" :
                    "I2C0 (LIS): NOT READY\r\n");
    usb_send_string(device_is_ready(fdc_i2c) ?
                    "I2C1 (FDC): READY\r\n" :
                    "I2C1 (FDC): NOT READY\r\n");

    print_probe_status("LIS2DTW12 0x18", lis18, false);
    print_probe_status("LIS2DTW12 0x19", lis19, false);
    print_probe_status("FDC1004   0x50", fdc, true);
    usb_send_string("---------------------------------\r\n");
}

static void print_startup(bool led_ready, bool btn_ready, bool adc_ready)
{
    char buffer[96];

    usb_send_string("\r\n=================================\r\n");
    usb_send_string(" APSLIM HW BRING-UP / LIS2DTW12\r\n");
    usb_send_string("=================================\r\n");
    snprintf(buffer, sizeof(buffer),
             "LED P0.31 : %s | BTN P0.26 : %s\r\n",
             led_ready ? "READY" : "FAIL",
             btn_ready ? "READY" : "FAIL");
    usb_send_string(buffer);
    snprintf(buffer, sizeof(buffer), "BAT ADC P0.02 : %s\r\n",
             adc_ready ? "READY" : "FAIL");
    usb_send_string(buffer);
    usb_send_string("LIS I2C0: SDA=P0.06 SCL=P0.08\r\n");
    usb_send_string("FDC I2C1: SDA=P1.00 SCL=P0.25\r\n");
    usb_send_string("I2C interval: 20 ms (reg reads)\r\n");
    usb_send_string("Expected: LIS WHO=0x44, FDC ID=0x1004\r\n");
}

int main(void)
{
    bool led_ready = false;
    bool btn_ready = false;
    bool adc_ready = false;
    bool usb_ready = false;
    bool terminal_connected = false;

    int ret;
    int btn_raw = 0;
    int btn_last_raw = 0;
    int btn_stable = 0;
    int16_t adc_raw = 0;
    int32_t adc_mv = 0;
    uint32_t dtr = 0;
    char buffer[128];

    int64_t now;
    int64_t last_led_time = 0;
    int64_t last_btn_change_time = 0;
    int64_t last_adc_time = 0;
    int64_t last_i2c_time = 0;
    int64_t last_i2c_report_time = 0;

    struct i2c_probe lis18 = { .last_ret = -ENODEV };
    struct i2c_probe lis19 = { .last_ret = -ENODEV };
    struct i2c_probe fdc   = { .last_ret = -ENODEV };

    struct adc_sequence adc_seq = {
        .buffer = &adc_raw,
        .buffer_size = sizeof(adc_raw),
    };

    /* GPIO and ADC are non-fatal: I2C probing must still run. */
    if (gpio_is_ready_dt(&debug_led1)) {
        led_ready = (gpio_pin_configure_dt(&debug_led1,
                                          GPIO_OUTPUT_INACTIVE) == 0);
    }
    if (gpio_is_ready_dt(&button)) {
        btn_ready = (gpio_pin_configure_dt(&button, GPIO_INPUT) == 0);
        if (btn_ready) {
            btn_raw = gpio_pin_get_dt(&button);
            if (btn_raw < 0) {
                btn_ready = false;
            } else {
                btn_last_raw = btn_raw;
                btn_stable = btn_raw;
            }
        }
    }
    if (adc_is_ready_dt(&battery_adc) &&
        adc_channel_setup_dt(&battery_adc) == 0 &&
        adc_sequence_init_dt(&battery_adc, &adc_seq) == 0) {
        adc_ready = true;
    }

    if (device_is_ready(cdc_dev) && (usb_enable(NULL) == 0)) {
        usb_ready = true;
    }

    while (1) {
        now = k_uptime_get();

        /* LED: hardware power-on heartbeat (independent of USB). */
        if (led_ready && (now - last_led_time >= LED_TOGGLE_PERIOD_MS)) {
            last_led_time = now;
            (void)gpio_pin_toggle_dt(&debug_led1);
        }

        /* Poll actual I2C hardware continuously, even without USB CDC. */
        if (now - last_i2c_time >= I2C_POLL_PERIOD_MS) {
            last_i2c_time = now;
            probe_lis2dtw12(LIS2DTW12_ADDR_LOW, &lis18);
            probe_lis2dtw12(LIS2DTW12_ADDR_HIGH, &lis19);
            probe_fdc1004(&fdc);
        }

        if (usb_ready) {
            dtr = 0;
            ret = uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
            if (ret == 0 && dtr != 0U) {
                if (!terminal_connected) {
                    terminal_connected = true;
                    print_startup(led_ready, btn_ready, adc_ready);
                    print_i2c_status(&lis18, &lis19, &fdc);
                    last_i2c_report_time = now;
                }
            } else {
                terminal_connected = false;
            }
        }

        /* Button: 30 ms software debounce. */
        if (btn_ready) {
            btn_raw = gpio_pin_get_dt(&button);
            if (btn_raw >= 0) {
                if (btn_raw != btn_last_raw) {
                    btn_last_raw = btn_raw;
                    last_btn_change_time = now;
                }
                if (now - last_btn_change_time >= BUTTON_DEBOUNCE_MS &&
                    btn_raw != btn_stable) {
                    btn_stable = btn_raw;
                    if (terminal_connected) {
                        snprintf(buffer, sizeof(buffer),
                                 "BTN : %d\r\n", btn_stable);
                        usb_send_string(buffer);
                    }
                }
            }
        }

        /* Battery ADC: original 500 ms interval. */
        if (now - last_adc_time >= ADC_SAMPLE_PERIOD_MS) {
            last_adc_time = now;
            if (adc_ready) {
                ret = adc_read_dt(&battery_adc, &adc_seq);
                if (terminal_connected) {
                    if (ret < 0) {
                        snprintf(buffer, sizeof(buffer),
                                 "BAT ADC ERROR : %d\r\n", ret);
                    } else {
                        adc_mv = adc_raw;
                        ret = adc_raw_to_millivolts_dt(&battery_adc,
                                                       &adc_mv);
                        if (ret == 0) {
                            snprintf(buffer, sizeof(buffer),
                                     "BAT ADC RAW : %d | ADC PIN : %ld mV\r\n",
                                     adc_raw, (long)adc_mv);
                        } else {
                            snprintf(buffer, sizeof(buffer),
                                     "BAT ADC RAW : %d\r\n", adc_raw);
                        }
                    }
                    usb_send_string(buffer);
                }
            }
        }

        if (terminal_connected &&
            (now - last_i2c_report_time >= I2C_REPORT_PERIOD_MS)) {
            last_i2c_report_time = now;
            print_i2c_status(&lis18, &lis19, &fdc);
        }

        k_msleep(MAIN_LOOP_PERIOD_MS);
    }

    return 0;
}
