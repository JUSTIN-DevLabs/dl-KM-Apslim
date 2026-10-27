#include "capacitive_service.h"

#include <string.h>

#include "app_config.h"

int capacitive_service_init(capacitive_service_t *service, const sensor_bus_t *bus,
                            const fdc1004_config_t *config, uint8_t filter_samples)
{
    memset(service, 0, sizeof(*service));
    service->status.sample_ret = -ENODATA;
    int ret = moving_average_init(&service->filter, filter_samples);
    if (ret == 0) {
        ret = fdc1004_init(&service->driver, bus, config);
    }
    service->status.init_ret = ret;
    return ret;
}

bool capacitive_service_process(capacitive_service_t *service, uint32_t now_ms, bool sample_due)
{
    if (service->status.init_ret < 0) {
        return false;
    }
    bool is_new = false;
    int ret = fdc1004_process(&service->driver, now_ms, sample_due, &is_new);
    if (ret < 0) {
        service->status.sample_ret = ret;
        service->status.is_filter_ready = false;
        moving_average_reset(&service->filter);
        if (!service->driver.is_ranging) {
            service->status.has_sample = false;
        }
        return false;
    }
    if (!is_new) {
        return false;
    }
    if (!service->status.has_sample || now_ms - service->status.sample_ms > APP_FDC_STALE_MS) {
        moving_average_reset(&service->filter);
    }
    service->filtered_pf_x10000 =
        moving_average_push(&service->filter, service->driver.raw_pf_x10000);
    service->status.sample_ret = 0;
    service->status.has_sample = true;
    service->status.sample_ms = now_ms;
    service->status.sequence++;
    service->status.is_filter_ready = service->filter.count == service->filter.window;
    return true;
}

void capacitive_service_stop(capacitive_service_t *service)
{
    (void)fdc1004_stop(&service->driver);
    service->status.has_sample = false;
    service->status.is_filter_ready = false;
}
