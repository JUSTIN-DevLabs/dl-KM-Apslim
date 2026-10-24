/**
 * @file lis2dh.c
 * @brief Register-level LIS2DH driver with injected bus callbacks.
 */
#include "lis2dh.h"

#include <errno.h>
#include <stddef.h>

#define LIS2DH_I2C_ADDRESS_LOW       0x18U
#define LIS2DH_I2C_ADDRESS_HIGH      0x19U
#define LIS2DH_REG_WHO_AM_I          0x0FU
#define LIS2DH_REG_CTRL1             0x20U
#define LIS2DH_REG_CTRL4             0x23U
#define LIS2DH_REG_OUT_X_L           0x28U
#define LIS2DH_REG_AUTO_INCREMENT    0x80U
#define LIS2DH_CTRL1_POWER_DOWN      0x00U
#define LIS2DH_CTRL1_ODR_100HZ_XYZ   0x57U
#define LIS2DH_CTRL4_BDU_HR_2G       0x88U
#define LIS2DH_AXIS_BUFFER_SIZE      6U
#define LIS2DH_SHIFT_BITS            4U
#define LIS2DH_12BIT_SIGN_BIT        0x0800U
#define LIS2DH_12BIT_SIGN_EXTEND     4096

static int16_t lis2dh_decode_mg(uint8_t low_byte, uint8_t high_byte);

int lis2dh_init(lis2dh_device_t *p_device, const lis2dh_bus_t *p_bus)
{
    if ((p_device == NULL) || (p_bus == NULL) ||
        (p_bus->read == NULL) || (p_bus->write == NULL)) {
        return -EINVAL;
    }

    p_device->is_initialized = false;
    p_device->bus = *p_bus;
    p_device->i2c_address = 0U;

    const uint8_t addresses[] = {
        LIS2DH_I2C_ADDRESS_LOW,
        LIS2DH_I2C_ADDRESS_HIGH,
    };
    uint8_t who_am_i = 0U;
    bool is_found = false;

    for (size_t i = 0U; i < (sizeof(addresses) / sizeof(addresses[0])); i++) {
        int ret = p_bus->read(p_bus->p_context, addresses[i],
                              LIS2DH_REG_WHO_AM_I, &who_am_i, sizeof(who_am_i));
        if ((ret == 0) && (who_am_i == LIS2DH_WHO_AM_I_VALUE)) {
            p_device->i2c_address = addresses[i];
            is_found = true;
            break;
        }
    }

    if (!is_found) {
        return -ENODEV;
    }

    /* Configure HR and BDU before enabling measurement output. */
    int ret = p_bus->write(p_bus->p_context, p_device->i2c_address,
                           LIS2DH_REG_CTRL1, LIS2DH_CTRL1_POWER_DOWN);
    if (ret < 0) {
        return ret;
    }

    ret = p_bus->write(p_bus->p_context, p_device->i2c_address,
                       LIS2DH_REG_CTRL4, LIS2DH_CTRL4_BDU_HR_2G);
    if (ret < 0) {
        return ret;
    }

    ret = p_bus->write(p_bus->p_context, p_device->i2c_address,
                       LIS2DH_REG_CTRL1, LIS2DH_CTRL1_ODR_100HZ_XYZ);
    if (ret < 0) {
        return ret;
    }

    p_device->is_initialized = true;
    return 0;
}

int lis2dh_read_accel(lis2dh_device_t *p_device, lis2dh_sample_t *p_sample)
{
    if ((p_device == NULL) || (p_sample == NULL)) {
        return -EINVAL;
    }

    if (!p_device->is_initialized) {
        return -ENODEV;
    }

    uint8_t axis_bytes[LIS2DH_AXIS_BUFFER_SIZE] = {0};
    uint8_t start_register = LIS2DH_REG_OUT_X_L | LIS2DH_REG_AUTO_INCREMENT;

    int ret = p_device->bus.read(p_device->bus.p_context, p_device->i2c_address,
                                 start_register, axis_bytes, sizeof(axis_bytes));
    if (ret < 0) {
        return ret;
    }

    lis2dh_sample_t sample = {
        .x_mg = lis2dh_decode_mg(axis_bytes[0], axis_bytes[1]),
        .y_mg = lis2dh_decode_mg(axis_bytes[2], axis_bytes[3]),
        .z_mg = lis2dh_decode_mg(axis_bytes[4], axis_bytes[5]),
    };

    *p_sample = sample;
    return 0;
}

static int16_t lis2dh_decode_mg(uint8_t low_byte, uint8_t high_byte)
{
    uint16_t packed = (uint16_t)low_byte | ((uint16_t)high_byte << 8U);
    uint16_t raw_12bit = packed >> LIS2DH_SHIFT_BITS;

    /* Explicit 12-bit sign extension avoids implementation-defined signed shifts. */
    int32_t signed_value = ((raw_12bit & LIS2DH_12BIT_SIGN_BIT) != 0U)
        ? (int32_t)raw_12bit - LIS2DH_12BIT_SIGN_EXTEND
        : (int32_t)raw_12bit;

    /* At +/-2 g in HR mode, one 12-bit LSB equals approximately 1 mg. */
    return (int16_t)signed_value;
}
