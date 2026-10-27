#ifndef APSLIM_POWER_SERVICE_H_
#define APSLIM_POWER_SERVICE_H_
#include <stdbool.h>

typedef struct {
    bool is_on;
    int init_ret;
    int last_ret;
} power_service_t;
int power_service_init(power_service_t *power);
bool power_service_update(power_service_t *power, bool has_long_press_event);

#endif
