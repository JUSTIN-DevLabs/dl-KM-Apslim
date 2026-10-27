#ifndef APSLIM_SENSOR_STATUS_H_
#define APSLIM_SENSOR_STATUS_H_
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int init_ret;
    int sample_ret;
    uint32_t sample_ms;
    uint32_t sequence;
    bool has_sample;
    bool is_filter_ready;
} sensor_status_t;

static inline int sensor_status_check(const sensor_status_t *status, uint32_t now_ms,
                                      uint32_t stale_ms)
{
    if (status->init_ret < 0) {
        return status->init_ret;
    }
    if (status->sample_ret < 0) {
        return status->sample_ret;
    }
    if (!status->has_sample || !status->is_filter_ready) {
        return -ENODATA;
    }
    return now_ms - status->sample_ms > stale_ms ? -ETIMEDOUT : 0;
}
#endif
