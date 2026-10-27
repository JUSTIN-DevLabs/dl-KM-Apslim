#include "power_service.h"

#include "pwr_hal.h"

int power_service_init(power_service_t *power)
{
    power->is_on = false;
    power->init_ret = pwr_init();
    power->last_ret = power->init_ret;
    return power->last_ret;
}

bool power_service_update(power_service_t *power, bool has_long_press_event)
{
    if (!has_long_press_event || power->init_ret < 0) {
        return false;
    }
    bool is_next_on = !power->is_on;
    int ret = pwr_enable_set(is_next_on);
    power->last_ret = ret;
    if (ret < 0) {
        return false;
    }
    power->is_on = is_next_on;
    return true;
}
