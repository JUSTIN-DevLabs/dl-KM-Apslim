#ifndef APSLIM_ACCEL_SERVICE_H_
#define APSLIM_ACCEL_SERVICE_H_
#include "lis2dh12.h"
#include "moving_average.h"
#include "sensor_status.h"

typedef struct {
    lis2dh12_t driver;
    sensor_status_t status;
    moving_average_t filters[3];
    int32_t raw_mg[3];
    int32_t filtered_mg[3];
} accel_service_t;

int accel_service_init(accel_service_t *service, const sensor_bus_t *bus, uint32_t now_ms,
                       uint8_t filter_samples);
bool accel_service_process(accel_service_t *service, uint32_t now_ms, bool sample_due);
void accel_service_stop(accel_service_t *service);

#endif
