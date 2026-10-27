#ifndef APSLIM_BATTERY_SERVICE_H_
#define APSLIM_BATTERY_SERVICE_H_
#include "sensor_status.h"
typedef struct {
    sensor_status_t status;
    int16_t raw;
    int32_t pin_mv;
    int32_t battery_mv;
} battery_service_t;
int battery_service_init(battery_service_t *service);
void battery_service_process(battery_service_t *service, uint32_t now_ms);
#endif
