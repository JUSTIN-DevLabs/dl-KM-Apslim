#ifndef APSLIM_I2C_HAL_H_
#define APSLIM_I2C_HAL_H_
#include "sensor_bus.h"
const sensor_bus_t *i2c_hal_accel_bus(void);
const sensor_bus_t *i2c_hal_capacitive_bus(void);
#endif
