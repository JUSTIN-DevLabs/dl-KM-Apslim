#include "i2c_hal.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/i2c.h>

static int i2c_bus_read(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t length)
{
    const struct device *dev = context;
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    return i2c_write_read(dev, address, &reg, 1U, data, length);
}

static int i2c_bus_write(void *context, uint8_t address, uint8_t reg, const uint8_t *data,
                         size_t length)
{
    const struct device *dev = context;
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    if (length > 7U) {
        return -EINVAL;
    }
    uint8_t bytes[8];
    bytes[0] = reg;
    memcpy(&bytes[1], data, length);
    return i2c_write(dev, bytes, length + 1U, address);
}

static const sensor_bus_t s_accel_bus = {
    .context = (void *)DEVICE_DT_GET(DT_NODELABEL(i2c0)),
    .read = i2c_bus_read,
    .write = i2c_bus_write,
};
static const sensor_bus_t s_cap_bus = {
    .context = (void *)DEVICE_DT_GET(DT_NODELABEL(i2c1)),
    .read = i2c_bus_read,
    .write = i2c_bus_write,
};

const sensor_bus_t *i2c_hal_accel_bus(void)
{
    return &s_accel_bus;
}
const sensor_bus_t *i2c_hal_capacitive_bus(void)
{
    return &s_cap_bus;
}
