#ifndef APSLIM_CAPACITIVE_SERVICE_H_
#define APSLIM_CAPACITIVE_SERVICE_H_
#include "fdc1004.h"
#include "moving_average.h"
#include "sensor_status.h"

typedef struct {
    fdc1004_t driver;
    sensor_status_t status;
    moving_average_t filter;
    int32_t filtered_pf_x10000;
} capacitive_service_t;

int capacitive_service_init(capacitive_service_t *service, const sensor_bus_t *bus,
                            const fdc1004_config_t *config, uint8_t filter_samples);
bool capacitive_service_process(capacitive_service_t *service, uint32_t now_ms, bool sample_due);
void capacitive_service_stop(capacitive_service_t *service);

#endif
