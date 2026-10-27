#ifndef APSLIM_SENSOR_BUS_H_
#define APSLIM_SENSOR_BUS_H_
#include <stddef.h>
#include <stdint.h>

/* Minimal transport seam for the two IC drivers; register addresses are IC-specific.
 * Callbacks return 0 on success / negative errno. Buffers are borrowed for the call only.
 * Bus transfers may block inside the driver; call from main, never the TIMER2 ISR.
 */
typedef struct {
    void *context;
    int (*read)(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t length);
    int (*write)(void *context, uint8_t address, uint8_t reg, const uint8_t *data, size_t length);
} sensor_bus_t;

#endif
