/*
 * Project    : Apslim - FDC1004 CIN1 USB Test
 * Target SDK : nRF Connect SDK v3.4
 * MCU        : nRF52840 (MDBT50Q)
 *
 * I2C1       : SDA P1.00 / SCL P0.25
 * Debug LED  : P0.31, Active Low
 * Power Hold : P0.05, High
 *
 * Filter     : Moving average only
 * Output     : raw = latest sample
 *              pF_MA = moving-average capacitance
 *
 * 기존 app.overlay / prj.conf 사용
 * 수정본의 실제 보드 빌드·동작 검증은 필요합니다.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* =========================================================
 * 사용자 조정 설정
 * ========================================================= */

/* 측정 시작 간격: 100ms → 약 10개/초, 20ms → 약 50개/초 */
#define SAMPLE_PERIOD_MS          50

/* 이동평균 개수: 1이면 평균 효과 없이 원본값 사용 */
#define MOVING_AVERAGE_COUNT        10

#define LED_TOGGLE_PERIOD_MS      500
#define SENSOR_TICK_MS              2
#define USB_SERVICE_PERIOD_MS      20
#define CONVERSION_TIMEOUT_MS      50
#define SENSOR_RETRY_MS          1000

/* 현재는 CAPDAC 비활성화 */
#define CAPDAC_STEPS                0U

/* =========================================================
 * FDC1004 레지스터
 * ========================================================= */

#define FDC_ADDR                  0x50

#define REG_MSB1                  0x00
#define REG_LSB1                  0x01
#define REG_MEAS_CONF1            0x08
#define REG_FDC_CONF              0x0C
#define REG_MANUFACTURER_ID       0xFE
#define REG_DEVICE_ID             0xFF

#define FDC_RATE_100_SPS          (1U << 10)
#define FDC_MEAS1_ENABLE          BIT(7)
#define FDC_DONE1                 BIT(3)

BUILD_ASSERT(MOVING_AVERAGE_COUNT >= 1,
             "MOVING_AVERAGE_COUNT must be at least 1");

/* 이 테스트 코드의 버퍼 메모리 상한 */
BUILD_ASSERT(MOVING_AVERAGE_COUNT <= 1024,
             "MOVING_AVERAGE_COUNT must not exceed 1024");

BUILD_ASSERT(SAMPLE_PERIOD_MS >= 20,
             "Use at least 20ms with this single-shot implementation");

BUILD_ASSERT(CAPDAC_STEPS <= 31U,
             "CAPDAC_STEPS must be 0..31");

/* =========================================================
 * 장치 및 타이머
 * ========================================================= */

static const struct device *const i2c_dev =
    DEVICE_DT_GET(DT_NODELABEL(i2c1));

static const struct device *const usb_dev =
    DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

static const struct gpio_dt_spec heartbeat =
    GPIO_DT_SPEC_GET(DT_ALIAS(fdc_heartbeat), gpios);

static const struct gpio_dt_spec power_hold =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pwr_enable_gpios);

K_TIMER_DEFINE(led_timer, NULL, NULL);
K_TIMER_DEFINE(sensor_timer, NULL, NULL);
K_TIMER_DEFINE(usb_timer, NULL, NULL);

/* =========================================================
 * 이동평균
 * 센서 처리 스레드에서만 접근
 * ========================================================= */

static int32_t ma_buffer[MOVING_AVERAGE_COUNT];
static int64_t ma_sum;
static uint32_t ma_index;
static uint32_t ma_count;

static void moving_average_reset(void)
{
    ma_sum = 0;
    ma_index = 0;
    ma_count = 0;

    /*
     * 버퍼는 유효 데이터 개수(ma_count)로 관리합니다.
     * 초기화 후 오래된 버퍼값은 읽지 않으므로
     * 배열 전체를 0으로 지울 필요가 없습니다.
     */
}

static void moving_average_update(int32_t raw)
{
    if (ma_count == MOVING_AVERAGE_COUNT) {
        ma_sum -= ma_buffer[ma_index];
    } else {
        ma_count++;
    }

    ma_buffer[ma_index] = raw;
    ma_sum += raw;

    ma_index++;

    if (ma_index >= MOVING_AVERAGE_COUNT) {
        ma_index = 0;
    }
}

static int64_t moving_average_cap_af(void)
{
    /*
     * 평균 raw를 먼저 정수로 잘라내지 않고
     * 합계에서 직접 aF로 환산합니다.
     *
     * pF = average(raw) / 524288 + CAPDAC * 3.125
     * 1 pF = 1,000,000 aF
     */
    if (ma_count == 0U) {
        return 0;
    }

    return (ma_sum * 1000000LL)
               / (524288LL * (int64_t)ma_count)
           + (int64_t)CAPDAC_STEPS * 3125000LL;
}

/* =========================================================
 * USB 전달 큐
 * ========================================================= */

struct sample {
    int64_t average_cap_af;
    int32_t raw;
    int error;
};

K_MSGQ_DEFINE(sample_queue, sizeof(struct sample), 8, 8);

static void publish(int err, int32_t raw, int64_t average_cap_af)
{
    struct sample data = {
        .average_cap_af = average_cap_af,
        .raw = raw,
        .error = err
    };

    if (k_msgq_put(&sample_queue, &data, K_NO_WAIT) != 0) {
        struct sample discarded;

        /* USB가 느리면 오래된 출력 데이터 제거 */
        (void)k_msgq_get(&sample_queue, &discarded, K_NO_WAIT);
        (void)k_msgq_put(&sample_queue, &data, K_NO_WAIT);
    }
}

/* =========================================================
 * FDC1004 I2C
 * ========================================================= */

static int read_reg(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];

    int err = i2c_write_read(
        i2c_dev, FDC_ADDR,
        &reg, 1,
        data, sizeof(data)
    );

    if (err == 0) {
        *value = ((uint16_t)data[0] << 8) | data[1];
    }

    return err;
}

static int write_reg(uint8_t reg, uint16_t value)
{
    const uint8_t data[] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)value
    };

    return i2c_write(i2c_dev, data, sizeof(data), FDC_ADDR);
}

static int sensor_init(void)
{
    uint16_t manufacturer;
    uint16_t device;
    int err;

    if (!device_is_ready(i2c_dev)) {
        return -ENODEV;
    }

    err = read_reg(REG_MANUFACTURER_ID, &manufacturer);
    if (err != 0) {
        return err;
    }

    err = read_reg(REG_DEVICE_ID, &device);
    if (err != 0) {
        return err;
    }

    if (manufacturer != 0x5449 || device != 0x1004) {
        return -ENODEV;
    }

    /* 이전 측정 중지 */
    err = write_reg(REG_FDC_CONF, FDC_RATE_100_SPS);
    if (err != 0) {
        return err;
    }

    /* CIN1 단일 입력 선택 */
    const uint16_t conf =
        (CAPDAC_STEPS == 0U)
        ? 0x1C00U
        : (0x1000U | (CAPDAC_STEPS << 5));

    return write_reg(REG_MEAS_CONF1, conf);
}

static int read_raw(int32_t *raw)
{
    uint16_t msb;
    uint16_t lsb;
    int err;

    err = read_reg(REG_MSB1, &msb);
    if (err != 0) {
        return err;
    }

    err = read_reg(REG_LSB1, &lsb);
    if (err != 0) {
        return err;
    }

    uint32_t value = ((uint32_t)msb << 8) | (lsb >> 8);

    *raw = (int32_t)value;

    /* signed 24-bit → signed 32-bit */
    if ((value & 0x800000U) != 0U) {
        *raw -= 0x1000000;
    }

    return 0;
}

/* =========================================================
 * 디버그 LED
 * ========================================================= */

static void led_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    if (!gpio_is_ready_dt(&heartbeat)) {
        return;
    }

    if (gpio_pin_configure_dt(
            &heartbeat, GPIO_OUTPUT_ACTIVE) != 0) {
        return;
    }

    k_timer_start(
        &led_timer,
        K_MSEC(LED_TOGGLE_PERIOD_MS),
        K_MSEC(LED_TOGGLE_PERIOD_MS)
    );

    for (;;) {
        (void)k_timer_status_sync(&led_timer);
        (void)gpio_pin_toggle_dt(&heartbeat);
    }
}

K_THREAD_DEFINE(
    led_tid, 768,
    led_thread,
    NULL, NULL, NULL,
    5, 0, 0
);

/* =========================================================
 * USB CDC
 * ========================================================= */

static bool usb_open(void)
{
    uint32_t dtr = 0;

    int err = uart_line_ctrl_get(
        usb_dev, UART_LINE_CTRL_DTR, &dtr
    );

    return (err == 0) && (dtr != 0U);
}

static void usb_send(const char *text)
{
    while (*text != '\0' && usb_open()) {
        uart_poll_out(usb_dev, (unsigned char)*text++);
    }
}

static void usb_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    bool was_open = false;

    if (!device_is_ready(usb_dev)) {
        return;
    }

    if (usb_enable(NULL) != 0) {
        return;
    }

    k_timer_start(
        &usb_timer,
        K_MSEC(USB_SERVICE_PERIOD_MS),
        K_MSEC(USB_SERVICE_PERIOD_MS)
    );

    for (;;) {
        (void)k_timer_status_sync(&usb_timer);

        bool connected = usb_open();

        if (connected && !was_open) {
            char header[160];

            snprintf(
                header, sizeof(header),
                "Target SDK: nRF Connect SDK v3.4\r\n"
                "CIN1 | period=%ums | moving_average=%u\r\n"
                "raw=latest | pF_MA=filtered\r\n",
                (unsigned int)SAMPLE_PERIOD_MS,
                (unsigned int)MOVING_AVERAGE_COUNT
            );

            usb_send(header);
        }

        was_open = connected;

        struct sample data;

        for (int n = 0; n < 8; ++n) {
            if (k_msgq_get(
                    &sample_queue, &data, K_NO_WAIT) != 0) {
                break;
            }

            if (!connected) {
                continue;
            }

            char line[128];

            if (data.error != 0) {
                snprintf(
                    line, sizeof(line),
                    "raw: N/A | pF_MA: N/A\r\n"
                );
            } else {
                int64_t cap_af = data.average_cap_af;

                uint32_t magnitude = (uint32_t)(
                    cap_af < 0 ? -cap_af : cap_af
                );

                snprintf(
                    line, sizeof(line),
                    "raw: %ld | pF_MA: %s%lu.%06lu\r\n",
                    (long)data.raw,
                    cap_af < 0 ? "-" : "",
                    (unsigned long)(magnitude / 1000000U),
                    (unsigned long)(magnitude % 1000000U)
                );
            }

            usb_send(line);
        }
    }
}

K_THREAD_DEFINE(
    usb_tid, 2048,
    usb_thread,
    NULL, NULL, NULL,
    7, 0, 0
);

/* =========================================================
 * 센서 상태 처리
 * ========================================================= */

int main(void)
{
    enum {
        NEED_INIT,
        IDLE,
        WAIT_DONE
    } state = NEED_INIT;

    int64_t next_sample = 0;
    int64_t deadline = 0;
    int err;

    if (!gpio_is_ready_dt(&power_hold)) {
        publish(-ENODEV, 0, 0);
        return 0;
    }

    err = gpio_pin_configure_dt(
        &power_hold, GPIO_OUTPUT_ACTIVE
    );

    if (err != 0) {
        publish(err, 0, 0);
        return 0;
    }

    moving_average_reset();

    k_timer_start(
        &sensor_timer,
        K_MSEC(SENSOR_TICK_MS),
        K_MSEC(SENSOR_TICK_MS)
    );

    for (;;) {
        (void)k_timer_status_sync(&sensor_timer);

        int64_t now = k_uptime_get();
        int32_t raw = 0;
        uint16_t status;

        err = 0;

        switch (state) {
        case NEED_INIT:
            if (now < next_sample) {
                break;
            }

            err = sensor_init();

            if (err == 0) {
                moving_average_reset();
                state = IDLE;
            }
            break;

        case IDLE:
            if (now < next_sample) {
                break;
            }

            /* 100S/s 설정, MEAS1 단발 측정 */
            err = write_reg(
                REG_FDC_CONF,
                FDC_RATE_100_SPS | FDC_MEAS1_ENABLE
            );

            if (err == 0) {
                deadline = now + CONVERSION_TIMEOUT_MS;
                next_sample = now + SAMPLE_PERIOD_MS;
                state = WAIT_DONE;
            }
            break;

        case WAIT_DONE:
            err = read_reg(REG_FDC_CONF, &status);

            if (err == 0 && (status & FDC_DONE1) != 0U) {
                err = read_raw(&raw);

                if (err == 0) {
                    /*
                     * 성공한 측정값만 이동평균에 반영.
                     * 초기에는 실제 수집된 개수로 나눔.
                     */
                    moving_average_update(raw);

                    publish(
                        0,
                        raw,
                        moving_average_cap_af()
                    );

                    state = IDLE;
                }
            } else if (err == 0 && now >= deadline) {
                err = -ETIMEDOUT;
            }
            break;
        }

        if (err != 0) {
            publish(err, 0, 0);

            /* 오류 전후의 오래된 값이 섞이지 않도록 초기화 */
            moving_average_reset();

            state = NEED_INIT;
            next_sample = k_uptime_get() + SENSOR_RETRY_MS;
        }
    }

    return 0;
}