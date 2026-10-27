#include "accel_service.h"

#include <string.h>

#include "app_config.h"

int accel_service_init(accel_service_t *service, const sensor_bus_t *bus, uint32_t now_ms,
                       uint8_t filter_samples)
{
    memset(service, 0, sizeof(*service));
    service->status.sample_ret = -ENODATA;
    for (uint8_t i = 0U; i < 3U; i++) {
        int ret = moving_average_init(&service->filters[i], filter_samples);
        if (ret < 0) {
            service->status.init_ret = ret;
            return ret;
        }
    }
    service->status.init_ret = lis2dh12_init(&service->driver, bus, now_ms);
    return service->status.init_ret;
}

bool accel_service_process(accel_service_t *service, uint32_t now_ms, bool sample_due)
{
    if (service->status.init_ret != 0 && service->status.init_ret != -EINPROGRESS) {
        return false;
    }
    service->status.init_ret = lis2dh12_init_process(&service->driver, now_ms);
    if (service->status.init_ret != 0 || !sample_due) {
        return false;
    }
    bool is_new = false;
    int ret = lis2dh12_read(&service->driver, service->raw_mg, &is_new);
    if (ret < 0) {
        service->status.sample_ret = ret;
        service->status.has_sample = false;
        service->status.is_filter_ready = false;
        for (uint8_t i = 0U; i < 3U; i++) {
            moving_average_reset(&service->filters[i]);
        }
        return false;
    }
    if (!is_new) {
        return false;
    }
    if (!service->status.has_sample || now_ms - service->status.sample_ms > APP_ACCEL_STALE_MS) {
        for (uint8_t i = 0U; i < 3U; i++) {
            moving_average_reset(&service->filters[i]);
        }
    }
    for (uint8_t i = 0U; i < 3U; i++) {
        service->filtered_mg[i] = moving_average_push(&service->filters[i], service->raw_mg[i]);
    }
    service->status.sample_ret = 0;
    service->status.has_sample = true;
    service->status.sample_ms = now_ms;
    service->status.sequence++;
    service->status.is_filter_ready = service->filters[0].count == service->filters[0].window;
    return true;
}

void accel_service_stop(accel_service_t *service)
{
    (void)lis2dh12_stop(&service->driver);
    service->status.has_sample = false;
    service->status.is_filter_ready = false;
}
