/**
 * @file lis2dh.h
 * @brief Portable LIS2DH/LIS2DH12 acceleration sensor interface.
 */
#ifndef LIS2DH_H_
#define LIS2DH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIS2DH_WHO_AM_I_VALUE 0x33U

typedef int (*lis2dh_read_fn_t)(void *p_context, uint8_t i2c_address,
                                uint8_t register_address, uint8_t *p_data,
                                size_t size_bytes);

typedef int (*lis2dh_write_fn_t)(void *p_context, uint8_t i2c_address,
                                 uint8_t register_address, uint8_t value);

typedef struct {
    lis2dh_read_fn_t read;
    lis2dh_write_fn_t write;
    void *p_context;
} lis2dh_bus_t;

typedef struct {
    lis2dh_bus_t bus;
    uint8_t i2c_address;
    bool is_initialized;
} lis2dh_device_t;

typedef struct {
    int16_t x_mg;
    int16_t y_mg;
    int16_t z_mg;
} lis2dh_sample_t;

/**
 * @brief Probe 0x18/0x19 and configure 100 Hz, +/-2 g, high-resolution mode.
 * @param[out] p_device Device context; must not be NULL.
 * @param[in] p_bus Caller-owned bus callbacks; must remain valid.
 * @retval 0 Success.
 * @retval -EINVAL Invalid arguments.
 * @retval -ENODEV Device not found or unexpected WHO_AM_I.
 * @return Negative bus error if register configuration fails.
 * @note Called from a thread/work context; never from an ISR.
 */
int lis2dh_init(lis2dh_device_t *p_device, const lis2dh_bus_t *p_bus);

/**
 * @brief Read XYZ acceleration in milli-g (mg).
 * @param[in] p_device Initialized device context.
 * @param[out] p_sample Output sample, unchanged on failure.
 * @retval 0 Success.
 * @retval -EINVAL Invalid arguments.
 * @retval -ENODEV Device is not initialized.
 * @return Negative bus error on I2C transfer failure.
 * @note Assumes the sensor remains in the configured +/-2 g, HR mode.
 * @note Caller synchronizes concurrent access, if necessary.
 */
int lis2dh_read_accel(lis2dh_device_t *p_device, lis2dh_sample_t *p_sample);

#endif /* LIS2DH_H_ */
