#include "fdc1004.h"

#include <errno.h>
#include <string.h>

#define FDC_ADDRESS 0x50U
#define FDC_CONFIG 0x0CU
#define FDC_MEAS_CONFIG 0x08U
#define FDC_CAPDAC_STEP_PF_X10000 31250L
#define FDC_RAW_PER_PF 524288L
#define FDC_INPUT_LIMIT_RAW (15L * FDC_RAW_PER_PF)

/* Private function prototypes */
static int fdc_read_register(fdc1004_t *sensor, uint8_t reg, uint16_t *value);
static int fdc_write_register(fdc1004_t *sensor, uint8_t reg, uint16_t value);
static int fdc_set_capdac(fdc1004_t *sensor, uint8_t capdac);
static int fdc_trigger(fdc1004_t *sensor, uint32_t now_ms);

/* Public functions */
int32_t fdc1004_decode_raw(uint16_t msb, uint16_t lsb)
{
    uint32_t value = ((uint32_t)msb << 8U) | ((uint32_t)lsb >> 8U);
    return (value & 0x800000U) != 0U ? (int32_t)value - 16777216L : (int32_t)value;
}

int32_t fdc1004_raw_to_pf_x10000(int32_t raw, uint8_t capdac)
{
    int64_t numerator = (int64_t)raw * 10000LL;
    int64_t rounded =
        numerator >= 0 ? numerator + FDC_RAW_PER_PF / 2 : numerator - FDC_RAW_PER_PF / 2;
    return (int32_t)((rounded / FDC_RAW_PER_PF) + (int64_t)capdac * FDC_CAPDAC_STEP_PF_X10000);
}

int fdc1004_init(fdc1004_t *sensor, const sensor_bus_t *bus, const fdc1004_config_t *config)
{
    memset(sensor, 0, sizeof(*sensor));
    sensor->bus = bus;
    sensor->config = *config;
    sensor->init_ret = -ENODEV;
    if (bus == NULL || bus->read == NULL || bus->write == NULL || config->initial_capdac > 31U ||
        config->capdac_step == 0U || config->capdac_step > 31U || config->timeout_ms == 0U) {
        return -EINVAL;
    }
    int ret = fdc_read_register(sensor, 0xFEU, &sensor->manufacturer_id);
    if (ret == 0) {
        ret = fdc_read_register(sensor, 0xFFU, &sensor->device_id);
    }
    if (ret == 0 && (sensor->manufacturer_id != 0x5449U || sensor->device_id != 0x1004U)) {
        ret = -ENODEV;
    }
    if (ret == 0) {
        ret = fdc_write_register(sensor, FDC_CONFIG, 0U);
    }
    if (ret == 0) {
        ret = fdc_set_capdac(sensor, config->initial_capdac);
    }
    sensor->init_ret = ret;
    return ret;
}

int fdc1004_process(fdc1004_t *sensor, uint32_t now_ms, bool should_trigger, bool *is_new)
{
    *is_new = false;
    if (sensor->init_ret < 0) {
        return sensor->init_ret;
    }
    if (sensor->is_pending) {
        uint16_t status = 0U;
        int ret = fdc_read_register(sensor, FDC_CONFIG, &status);
        if (ret == 0 && (status & 0x0008U) == 0U) {
            if (now_ms - sensor->trigger_ms < sensor->config.timeout_ms) {
                return 0;
            }
            ret = -ETIMEDOUT;
        }
        uint16_t msb = 0U;
        uint16_t lsb = 0U;
        if (ret == 0) {
            ret = fdc_read_register(sensor, 0x00U, &msb);
        }
        if (ret == 0) {
            ret = fdc_read_register(sensor, 0x01U, &lsb);
        }
        sensor->is_pending = false;
        if (ret < 0) {
            sensor->is_raw_valid = false;
            sensor->is_ranging = false;
            return ret;
        }
        sensor->raw_ms = now_ms;
        sensor->raw_sequence++;
        sensor->raw_capdac = sensor->capdac;
        sensor->raw_code = fdc1004_decode_raw(msb, lsb);
        sensor->raw_pf_x10000 = fdc1004_raw_to_pf_x10000(sensor->raw_code, sensor->raw_capdac);
        sensor->is_raw_valid =
            sensor->raw_code > -FDC_INPUT_LIMIT_RAW && sensor->raw_code < FDC_INPUT_LIMIT_RAW;
        if (!sensor->is_raw_valid) {
            uint8_t next = sensor->capdac;
            if (sensor->config.is_auto_range_enabled) {
                if (sensor->raw_code >= FDC_INPUT_LIMIT_RAW && next < 31U) {
                    unsigned int sum = next + sensor->config.capdac_step;
                    next = (uint8_t)(sum > 31U ? 31U : sum);
                } else if (sensor->raw_code <= -FDC_INPUT_LIMIT_RAW && next > 0U) {
                    next = next > sensor->config.capdac_step
                               ? (uint8_t)(next - sensor->config.capdac_step)
                               : 0U;
                }
            }
            sensor->is_ranging = next != sensor->capdac;
            if (!sensor->is_ranging) {
                return -ERANGE;
            }
            ret = fdc_set_capdac(sensor, next);
            if (ret == 0) {
                ret = fdc_trigger(sensor, now_ms);
            }
            if (ret < 0) {
                sensor->is_ranging = false;
                return ret;
            }
            return -EAGAIN;
        }
        sensor->is_ranging = false;
        *is_new = true;
    }
    if (should_trigger && !sensor->is_pending) {
        int ret = fdc_trigger(sensor, now_ms);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int fdc1004_stop(fdc1004_t *sensor)
{
    int ret = sensor->bus != NULL && sensor->init_ret == 0
                  ? fdc_write_register(sensor, FDC_CONFIG, 0U)
                  : 0;
    sensor->is_pending = false;
    sensor->is_ranging = false;
    sensor->init_ret = -EINPROGRESS;
    return ret;
}

/* Private functions */
static int fdc_read_register(fdc1004_t *sensor, uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    int ret = sensor->bus->read(sensor->bus->context, FDC_ADDRESS, reg, data, sizeof(data));
    if (ret == 0) {
        *value = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    }
    return ret;
}

static int fdc_write_register(fdc1004_t *sensor, uint8_t reg, uint16_t value)
{
    uint8_t data[2] = {(uint8_t)(value >> 8U), (uint8_t)value};
    return sensor->bus->write(sensor->bus->context, FDC_ADDRESS, reg, data, sizeof(data));
}

static int fdc_set_capdac(fdc1004_t *sensor, uint8_t capdac)
{
    int ret =
        fdc_write_register(sensor, FDC_MEAS_CONFIG, (uint16_t)(0x1000U | ((uint32_t)capdac << 5U)));
    if (ret == 0) {
        sensor->capdac = capdac;
    }
    return ret;
}

static int fdc_trigger(fdc1004_t *sensor, uint32_t now_ms)
{
    /* CHA=CIN1, CHB=CAPDAC; single MEAS1 at 100 S/s, as in the supplied source. */
    int ret = fdc_write_register(sensor, FDC_CONFIG, 0x0480U);
    if (ret == 0) {
        sensor->is_pending = true;
        sensor->trigger_ms = now_ms;
    }
    return ret;
}
