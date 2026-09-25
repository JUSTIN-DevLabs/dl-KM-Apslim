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


/* =========================================================
 * DEBUG LED1
 *
 * P0.31
 * ========================================================= */

#define DEBUG_LED1_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec debug_led1 =
    GPIO_DT_SPEC_GET(DEBUG_LED1_NODE, gpios);


/* =========================================================
 * BUTTON
 *
 * P0.26
 * ========================================================= */

#define BTN_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(BTN_NODE, gpios);


/* =========================================================
 * BATTERY ADC
 *
 * P0.02 / AIN0
 * ========================================================= */

static const struct adc_dt_spec battery_adc =
    ADC_DT_SPEC_GET_BY_NAME(
        DT_PATH(zephyr_user),
        battery_adc
    );


/* =========================================================
 * I2C
 * ========================================================= */

static const struct device *lis_i2c =
    DEVICE_DT_GET(DT_NODELABEL(i2c0));

static const struct device *fdc_i2c =
    DEVICE_DT_GET(DT_NODELABEL(i2c1));


/* =========================================================
 * USB CDC
 * ========================================================= */

static const struct device *cdc_dev =
    DEVICE_DT_GET(DT_NODELABEL(usb_cdc));


/* =========================================================
 * I2C DEVICE DEFINITIONS
 * ========================================================= */

/* LIS2DH12 */
#define LIS2DH12_ADDR_LOW       0x18
#define LIS2DH12_ADDR_HIGH      0x19

#define LIS2DH12_WHO_AM_I_REG   0x0F
#define LIS2DH12_WHO_AM_I_VALUE 0x33


/* FDC1004 */
#define FDC1004_ADDR            0x50

#define FDC1004_DEVICE_ID_REG   0xFF
#define FDC1004_DEVICE_ID_VALUE 0x1004


/* =========================================================
 * USB Send
 * ========================================================= */

static void usb_send_string(const char *str)
{
    while (*str != '\0')
    {
        uart_poll_out(cdc_dev, *str);
        str++;
    }
}


/* =========================================================
 * LIS2DH12 Address Check
 *
 * 0x18 / 0x19 둘 다 확인
 * WHO_AM_I = 0x33 확인
 * ========================================================= */

static int lis2dh12_detect(uint8_t *found_addr)
{
    uint8_t who_am_i;

    const uint8_t address_list[] = {
        LIS2DH12_ADDR_LOW,
        LIS2DH12_ADDR_HIGH
    };

    for (int i = 0; i < 2; i++)
    {
        int ret = i2c_reg_read_byte(
            lis_i2c,
            address_list[i],
            LIS2DH12_WHO_AM_I_REG,
            &who_am_i
        );

        if ((ret == 0) &&
            (who_am_i == LIS2DH12_WHO_AM_I_VALUE))
        {
            *found_addr = address_list[i];

            return 0;
        }
    }

    return -1;
}


/* =========================================================
 * FDC1004 16-bit Register Read
 * ========================================================= */

static int fdc1004_read_register(
    uint8_t reg,
    uint16_t *value)
{
    uint8_t rx_data[2];

    int ret = i2c_write_read(
        fdc_i2c,
        FDC1004_ADDR,
        &reg,
        1,
        rx_data,
        2
    );

    if (ret != 0)
    {
        return ret;
    }

    *value =
        ((uint16_t)rx_data[0] << 8) |
        ((uint16_t)rx_data[1]);

    return 0;
}


/* =========================================================
 * Print I2C Device Status
 * ========================================================= */

static void print_i2c_device_status(void)
{
    uint8_t lis_addr = 0;
    uint16_t fdc_device_id = 0;

    char buffer[96];


    usb_send_string(
        "----------------------------\r\n"
    );

    usb_send_string(
        " I2C DEVICE CHECK\r\n"
    );

    usb_send_string(
        "----------------------------\r\n"
    );


    /* -----------------------------------------------------
     * LIS2DH12
     * ----------------------------------------------------- */

    if (device_is_ready(lis_i2c) &&
        (lis2dh12_detect(&lis_addr) == 0))
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "LIS2DH12 : 0x%02X\r\n",
            lis_addr
        );
    }
    else
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "LIS2DH12 : NONE\r\n"
        );
    }

    usb_send_string(buffer);


    /* -----------------------------------------------------
     * FDC1004
     * ----------------------------------------------------- */

    if (device_is_ready(fdc_i2c) &&
        (fdc1004_read_register(
            FDC1004_DEVICE_ID_REG,
            &fdc_device_id) == 0) &&
        (fdc_device_id ==
            FDC1004_DEVICE_ID_VALUE))
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "FDC1004  : 0x%02X\r\n",
            FDC1004_ADDR
        );
    }
    else
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "FDC1004  : NONE\r\n"
        );
    }

    usb_send_string(buffer);

    usb_send_string(
        "----------------------------\r\n\r\n"
    );
}


/* =========================================================
 * Main
 * ========================================================= */

int main(void)
{
    int ret;

    bool terminal_connected = false;

    uint32_t dtr = 0;

    int16_t adc_raw = 0;
    int32_t adc_mv = 0;

    char tx_buffer[128];


    /* =====================================================
     * Timing
     * ===================================================== */

    int64_t now;

    int64_t last_led_time = 0;
    int64_t last_adc_time = 0;

    int64_t btn_change_time = 0;


    /* =====================================================
     * Button State
     * ===================================================== */

    int btn_raw;
    int btn_last_raw;
    int btn_stable;


    /* =====================================================
     * GPIO Init
     * ===================================================== */

    if (!gpio_is_ready_dt(&debug_led1))
    {
        return 0;
    }

    if (!gpio_is_ready_dt(&button))
    {
        return 0;
    }


    ret = gpio_pin_configure_dt(
        &debug_led1,
        GPIO_OUTPUT_INACTIVE
    );

    if (ret < 0)
    {
        return 0;
    }


    ret = gpio_pin_configure_dt(
        &button,
        GPIO_INPUT
    );

    if (ret < 0)
    {
        return 0;
    }


    /* =====================================================
     * Button Initial State
     * ===================================================== */

    btn_raw = gpio_pin_get_dt(&button);

    btn_last_raw = btn_raw;
    btn_stable = btn_raw;


    /* =====================================================
     * ADC Init
     * ===================================================== */

    if (!adc_is_ready_dt(&battery_adc))
    {
        return 0;
    }


    ret = adc_channel_setup_dt(
        &battery_adc
    );

    if (ret < 0)
    {
        return 0;
    }


    struct adc_sequence sequence = {
        .buffer = &adc_raw,
        .buffer_size = sizeof(adc_raw),
    };


    ret = adc_sequence_init_dt(
        &battery_adc,
        &sequence
    );

    if (ret < 0)
    {
        return 0;
    }


    /* =====================================================
     * USB Init
     * ===================================================== */

    if (!device_is_ready(cdc_dev))
    {
        return 0;
    }


    ret = usb_enable(NULL);

    if (ret != 0)
    {
        return 0;
    }


    /* =====================================================
     * Main Loop
     * ===================================================== */

    while (1)
    {
        now = k_uptime_get();


        /* =================================================
         * DEBUG LED1
         *
         * USB 상태와 무관
         * 500 ms Toggle
         * ================================================= */

        if ((now - last_led_time) >= 500)
        {
            last_led_time = now;

            gpio_pin_toggle_dt(
                &debug_led1
            );
        }


        /* =================================================
         * USB Terminal State
         * ================================================= */

        dtr = 0;

        uart_line_ctrl_get(
            cdc_dev,
            UART_LINE_CTRL_DTR,
            &dtr
        );


        /* =================================================
         * Terminal Connected
         * ================================================= */

        if ((dtr != 0U) &&
            (terminal_connected == false))
        {
            terminal_connected = true;


            usb_send_string("\r\n");
            usb_send_string("============================\r\n");
            usb_send_string(" APSLIM HW TEST\r\n");
            usb_send_string("============================\r\n");

            usb_send_string(
                "DEBUG LED : P0.31\r\n"
            );

            usb_send_string(
                "BUTTON    : P0.26\r\n"
            );

            usb_send_string(
                "BAT ADC   : P0.02 / AIN0\r\n"
            );

            usb_send_string(
                "SPOON ADC : P0.03 / AIN1\r\n"
            );

            usb_send_string("\r\n");


            /* =============================================
             * I2C Address Check
             *
             * Terminal을 열었을 때 한 번 실행
             * ============================================= */

            print_i2c_device_status();
        }


        /* =================================================
         * Terminal Disconnected
         * ================================================= */

        if (dtr == 0U)
        {
            terminal_connected = false;
        }


        /* =================================================
         * BUTTON
         *
         * 30 ms Debounce
         * ================================================= */

        btn_raw =
            gpio_pin_get_dt(&button);


        if (btn_raw != btn_last_raw)
        {
            btn_last_raw = btn_raw;
            btn_change_time = now;
        }


        if (((now - btn_change_time) >= 30) &&
            (btn_raw != btn_stable))
        {
            btn_stable = btn_raw;


            if (terminal_connected)
            {
                snprintf(
                    tx_buffer,
                    sizeof(tx_buffer),
                    "BTN : %d\r\n",
                    btn_stable
                );

                usb_send_string(
                    tx_buffer
                );
            }
        }


        /* =================================================
         * BATTERY ADC
         *
         * 500 ms
         * ================================================= */

        if ((now - last_adc_time) >= 500)
        {
            last_adc_time = now;

            adc_raw = 0;


            ret = adc_read_dt(
                &battery_adc,
                &sequence
            );


            if (terminal_connected)
            {
                if (ret < 0)
                {
                    snprintf(
                        tx_buffer,
                        sizeof(tx_buffer),
                        "BAT ADC ERROR : %d\r\n",
                        ret
                    );
                }
                else
                {
                    adc_mv = adc_raw;

                    ret =
                        adc_raw_to_millivolts_dt(
                            &battery_adc,
                            &adc_mv
                        );


                    if (ret == 0)
                    {
                        snprintf(
                            tx_buffer,
                            sizeof(tx_buffer),
                            "BAT ADC RAW : %d | ADC PIN : %ld mV\r\n",
                            adc_raw,
                            (long)adc_mv
                        );
                    }
                    else
                    {
                        snprintf(
                            tx_buffer,
                            sizeof(tx_buffer),
                            "BAT ADC RAW : %d\r\n",
                            adc_raw
                        );
                    }
                }


                usb_send_string(
                    tx_buffer
                );
            }
        }


        /* =================================================
         * Main Task Period
         * ================================================= */

        k_msleep(10);
    }


    return 0;
}