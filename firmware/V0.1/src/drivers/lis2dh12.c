#include "lis2dh12.h"

#include <errno.h>
#include <string.h>

#define LIS_WHO_AM_I 0x0FU
#define LIS_CTRL1 0x20U
#define LIS_CTRL2 0x21U
#define LIS_STATUS 0x27U
#define LIS_OUTPUT 0x28U
#define LIS_DH_ID 0x33U
#define LIS_DW_ID 0x44U

/* Private function prototypes */
static int lis_reg_read(lis2dh12_t *sensor, uint8_t reg, uint8_t *value);
static int lis_reg_write(lis2dh12_t *sensor, uint8_t reg, uint8_t value);
static int lis_configure(lis2dh12_t *sensor);
static int32_t lis_decode_axis(const uint8_t *data, bool is_dh);

/* Public functions */
int lis2dh12_init(lis2dh12_t *sensor, const sensor_bus_t *bus, uint32_t now_ms)
{
    memset(sensor, 0, sizeof(*sensor));
    sensor->bus = bus;
    sensor->init_ret = -ENODEV;
    sensor->state = LIS_FAILED;
    if (bus == NULL || bus->read == NULL || bus->write == NULL) {
        return -EINVAL;
    }
    /* Source's 8-bit READ 0x33 is 7-bit address 0x19; it is not address 0x33. */
    static const uint8_t addresses[] = {0x19U, 0x18U};
    for (size_t i = 0U; i < sizeof(addresses); i++) {
        sensor->address = addresses[i];
        int ret = lis_reg_read(sensor, LIS_WHO_AM_I, &sensor->who_am_i);
        if (ret < 0) {
            sensor->init_ret = ret;
            continue;
        }
        if (sensor->who_am_i != LIS_DH_ID && sensor->who_am_i != LIS_DW_ID) {
            sensor->init_ret = -ENOTSUP;
            continue;
        }
        ret = sensor->who_am_i == LIS_DH_ID ? lis_configure(sensor)
                                            : lis_reg_write(sensor, LIS_CTRL2, 0x40U);
        if (ret < 0) {
            sensor->init_ret = ret;
            return ret;
        }
        sensor->since_ms = now_ms;
        sensor->state = sensor->who_am_i == LIS_DH_ID ? LIS_SETTLING : LIS_RESET_WAIT;
        sensor->init_ret = -EINPROGRESS;
        return -EINPROGRESS;
    }
    return sensor->init_ret;
}

int lis2dh12_init_process(lis2dh12_t *sensor, uint32_t now_ms)
{
    if (sensor->state == LIS_RESET_WAIT) {
        uint8_t ctrl2 = 0U;
        int ret = lis_reg_read(sensor, LIS_CTRL2, &ctrl2);
        if (ret == 0 && (ctrl2 & 0x40U) != 0U) {
            if (now_ms - sensor->since_ms < 100U) {
                return -EINPROGRESS;
            }
            ret = -ETIMEDOUT;
        }
        if (ret == 0) {
            ret = lis_configure(sensor);
        }
        if (ret < 0) {
            sensor->state = LIS_FAILED;
            sensor->init_ret = ret;
            return ret;
        }
        sensor->state = LIS_SETTLING;
        sensor->since_ms = now_ms;
    }
    uint32_t settle_ms = sensor->who_am_i == LIS_DH_ID ? 160U : 80U;
    if (sensor->state == LIS_SETTLING && now_ms - sensor->since_ms >= settle_ms) {
        sensor->state = LIS_READY;
        sensor->init_ret = 0;
    }
    return sensor->init_ret;
}

int lis2dh12_read(lis2dh12_t *sensor, int32_t axes_mg[3], bool *is_new)
{
    *is_new = false;
    if (sensor->state != LIS_READY) {
        return sensor->init_ret;
    }
    uint8_t status = 0U;
    int ret = lis_reg_read(sensor, LIS_STATUS, &status);
    bool is_dh = sensor->who_am_i == LIS_DH_ID;
    if (ret < 0) {
        return ret;
    }
    if ((status & (is_dh ? 0x08U : 0x01U)) == 0U) {
        return 0;
    }
    uint8_t bytes[6];
    uint8_t reg = is_dh ? (LIS_OUTPUT | 0x80U) : LIS_OUTPUT;
    ret = sensor->bus->read(sensor->bus->context, sensor->address, reg, bytes, sizeof(bytes));
    if (ret < 0) {
        return ret;
    }
    for (size_t i = 0U; i < 3U; i++) {
        axes_mg[i] = lis_decode_axis(&bytes[i * 2U], is_dh);
    }
    *is_new = true;
    return 0;
}

int lis2dh12_stop(lis2dh12_t *sensor)
{
    int ret = sensor->bus != NULL ? lis_reg_write(sensor, LIS_CTRL1, 0U) : 0;
    sensor->state = LIS_NOT_STARTED;
    sensor->init_ret = -EINPROGRESS;
    return ret;
}

const char *lis2dh12_name(const lis2dh12_t *sensor)
{
    if (sensor->who_am_i == LIS_DH_ID) {
        return "LIS2DH/DH12";
    }
    if (sensor->who_am_i == LIS_DW_ID) {
        return "LIS2DW12/DTW12";
    }
    return "NOT_DETECTED";
}

/* Private functions */
static int lis_reg_read(lis2dh12_t *sensor, uint8_t reg, uint8_t *value)
{
    return sensor->bus->read(sensor->bus->context, sensor->address, reg, value, 1U);
}

static int lis_reg_write(lis2dh12_t *sensor, uint8_t reg, uint8_t value)
{
    return sensor->bus->write(sensor->bus->context, sensor->address, reg, &value, 1U);
}

static int lis_configure(lis2dh12_t *sensor)
{
    /* Original 50 Hz settings, +/-2g. DH: 12-bit HR; DW: high performance. */
    static const uint8_t dh[][2] = {
        {0x20U, 0x00U}, {0x21U, 0x00U}, {0x22U, 0x00U}, {0x23U, 0x88U},
        {0x24U, 0x00U}, {0x25U, 0x00U}, {0x2EU, 0x00U}, {0x20U, 0x47U},
    };
    static const uint8_t dw[][2] = {
        {0x21U, 0x0CU},
        {0x25U, 0x00U},
        {0x2EU, 0x00U},
        {0x20U, 0x44U},
    };
    bool is_dh = sensor->who_am_i == LIS_DH_ID;
    const uint8_t (*settings)[2] = is_dh ? dh : dw;
    size_t count = is_dh ? sizeof(dh) / sizeof(dh[0]) : sizeof(dw) / sizeof(dw[0]);
    for (size_t i = 0U; i < count; i++) {
        int ret = lis_reg_write(sensor, settings[i][0], settings[i][1]);
        if (ret < 0) {
            return ret;
        }
    }
    uint8_t ctrl1 = 0U;
    int ret = lis_reg_read(sensor, LIS_CTRL1, &ctrl1);
    if (ret < 0) {
        return ret;
    }
    return ctrl1 == (is_dh ? 0x47U : 0x44U) ? 0 : -EIO;
}

static int32_t lis_decode_axis(const uint8_t *data, bool is_dh)
{
    int32_t raw = (int32_t)(((uint16_t)data[1] << 8U) | data[0]);
    if (raw >= 32768L) {
        raw -= 65536L;
    }
    int64_t numerator = is_dh ? raw : (int64_t)raw * 61;
    int32_t denominator = is_dh ? 16 : 1000;
    return (int32_t)((numerator >= 0 ? numerator + denominator / 2 : numerator - denominator / 2) /
                     denominator);
}
