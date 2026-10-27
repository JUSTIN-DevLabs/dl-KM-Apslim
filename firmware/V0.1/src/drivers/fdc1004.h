#ifndef APSLIM_FDC1004_H_
#define APSLIM_FDC1004_H_
#include <stdbool.h>
#include <stdint.h>

#include "sensor_bus.h"

typedef struct {
    uint8_t initial_capdac;
    uint8_t capdac_step;
    bool is_auto_range_enabled;
    uint32_t timeout_ms;
} fdc1004_config_t;
typedef struct {
    const sensor_bus_t *bus;
    fdc1004_config_t config;
    uint16_t manufacturer_id;
    uint16_t device_id;
    uint8_t capdac;
    uint8_t raw_capdac;
    uint32_t trigger_ms;
    uint32_t raw_ms;
    uint32_t raw_sequence;
    int32_t raw_code;
    int32_t raw_pf_x10000;
    bool is_raw_valid;
    bool is_pending;
    bool is_ranging;
    int init_ret;
} fdc1004_t;

/** All pointers must be non-NULL; bus and config are copied/referenced for sensor lifetime.
 * Returns 0 / negative errno; main context only. No application delay or busy wait.
 */
int fdc1004_init(fdc1004_t *sensor, const sensor_bus_t *bus, const fdc1004_config_t *config);
/* Non-NULL initialized sensor and is_new required.
 * Only is_new=true exposes an in-range new conversion to the filter.
 * -EAGAIN: range adjusted and retry started; -ERANGE: no usable range available.
 */
int fdc1004_process(fdc1004_t *sensor, uint32_t now_ms, bool should_trigger, bool *is_new);
int fdc1004_stop(fdc1004_t *sensor);
int32_t fdc1004_decode_raw(uint16_t msb, uint16_t lsb);
int32_t fdc1004_raw_to_pf_x10000(int32_t raw, uint8_t capdac);

#endif
