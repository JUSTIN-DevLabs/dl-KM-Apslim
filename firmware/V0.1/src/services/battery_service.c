#include "battery_service.h"

#include <string.h>

#include "app_config.h"
#include "battery_adc.h"

int battery_service_init(battery_service_t *service)
{
    memset(service, 0, sizeof(*service));
    service->status.sample_ret = -ENODATA;
    service->status.init_ret = battery_adc_init();
    return service->status.init_ret;
}

void battery_service_process(battery_service_t *service, uint32_t now_ms)
{
    if (service->status.init_ret < 0) {
        return;
    }
    int ret = battery_adc_read_raw(&service->raw);
    if (ret == 0) {
        ret = battery_adc_raw_to_mv(service->raw, &service->pin_mv);
    }
    if (ret == 0 && service->pin_mv < 0) {
        ret = -ERANGE;
    }
    service->status.sample_ret = ret;
    service->status.has_sample = ret == 0;
    service->status.is_filter_ready = ret == 0;
    if (ret == 0) {
        int64_t numerator =
            (int64_t)service->pin_mv * (APP_BATTERY_DIVIDER_HIGH_OHM + APP_BATTERY_DIVIDER_LOW_OHM);
        service->battery_mv =
            (int32_t)((numerator + APP_BATTERY_DIVIDER_LOW_OHM / 2U) / APP_BATTERY_DIVIDER_LOW_OHM);
        service->status.sample_ms = now_ms;
        service->status.sequence++;
    }
}
