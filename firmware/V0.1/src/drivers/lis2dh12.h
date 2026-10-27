#ifndef APSLIM_LIS2DH12_H_
#define APSLIM_LIS2DH12_H_
#include <stdbool.h>
#include <stdint.h>

#include "sensor_bus.h"

typedef enum { LIS_NOT_STARTED, LIS_RESET_WAIT, LIS_SETTLING, LIS_READY, LIS_FAILED } lis_state_t;
typedef struct {
    const sensor_bus_t *bus;
    lis_state_t state;
    uint8_t address;
    uint8_t who_am_i;
    uint32_t since_ms;
    int init_ret;
} lis2dh12_t;

/* Preserves source auto-detection: WHO_AM_I 0x33 (DH) or 0x44 (DW/DTW). */
int lis2dh12_init(lis2dh12_t *sensor, const sensor_bus_t *bus, uint32_t now_ms);
int lis2dh12_init_process(lis2dh12_t *sensor, uint32_t now_ms);
int lis2dh12_read(lis2dh12_t *sensor, int32_t axes_mg[3], bool *is_new);
int lis2dh12_stop(lis2dh12_t *sensor);
const char *lis2dh12_name(const lis2dh12_t *sensor);

#endif
