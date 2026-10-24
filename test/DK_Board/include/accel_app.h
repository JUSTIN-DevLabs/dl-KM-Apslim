/**
 * @file accel_app.h
 * @brief Board-specific LIS2DH validation application interface.
 */
#ifndef ACCEL_APP_H_
#define ACCEL_APP_H_

/**
 * @brief Initialize LED, I2C adapter, and periodic Zephyr kernel timers.
 * @retval 0 Success.
 * @retval -ENODEV Required board peripheral is unavailable.
 * @return Negative GPIO error if LED setup fails.
 * @note USB CDC ACM is initialized automatically through prj.conf.
 */
int accel_app_init(void);

#endif /* ACCEL_APP_H_ */
