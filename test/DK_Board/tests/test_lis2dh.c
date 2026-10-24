#include "lis2dh.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

static uint8_t s_config[256];

static int mock_read(void *p_context, uint8_t address, uint8_t reg,
                     uint8_t *p_data, size_t size_bytes)
{
    (void)p_context;
    if (address != 0x19U) {
        return -ENODEV;
    }
    if ((reg == 0x0FU) && (size_bytes == 1U)) {
        p_data[0] = 0x33U;
        return 0;
    }
    if ((reg == 0xA8U) && (size_bytes == 6U)) {
        /* 0 mg, -100 mg, +1000 mg in 12-bit HR output format. */
        const uint8_t samples[] = {0x00U, 0x00U, 0xC0U, 0xF9U, 0x80U, 0x3EU};
        for (size_t i = 0U; i < 6U; i++) {
            p_data[i] = samples[i];
        }
        return 0;
    }
    return -EIO;
}

static int mock_write(void *p_context, uint8_t address, uint8_t reg, uint8_t value)
{
    (void)p_context;
    if (address != 0x19U) {
        return -ENODEV;
    }
    s_config[reg] = value;
    return 0;
}

int main(void)
{
    lis2dh_device_t device = {0};
    lis2dh_bus_t bus = {
        .read = mock_read,
        .write = mock_write,
        .p_context = NULL,
    };

    assert(lis2dh_init(&device, &bus) == 0);
    assert(device.i2c_address == 0x19U);
    assert(s_config[0x20U] == 0x57U);
    assert(s_config[0x23U] == 0x88U);

    lis2dh_sample_t sample = {0};
    assert(lis2dh_read_accel(&device, &sample) == 0);
    assert(sample.x_mg == 0);
    assert(sample.y_mg == -100);
    assert(sample.z_mg == 1000);
    puts("Portable LIS2DH driver tests passed.");
    return 0;
}
