#include "accel_detect.h"

#include <limits.h>
#include <string.h>

void accel_detect_init(accel_detect_t *detector, const accel_detect_config_t *config)
{
    memset(detector, 0, sizeof(*detector));
    detector->config = *config;
}

void accel_detect_reset(accel_detect_t *detector)
{
    accel_detect_config_t config = detector->config;
    accel_detect_init(detector, &config);
}

void accel_detect_update(accel_detect_t *detector, uint32_t sample_ms, const int32_t axes_mg[3])
{
    /* Input is already filtered once by accel_service. No second moving average. */
    uint8_t index = detector->next;
    memcpy(detector->axes[index], axes_mg, sizeof(detector->axes[index]));
    detector->times_ms[index] = sample_ms;
    detector->next = (uint8_t)((index + 1U) % ACCEL_DETECT_HISTORY);
    if (detector->count < ACCEL_DETECT_HISTORY) {
        detector->count++;
    }
    int32_t low[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    int32_t high[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    uint8_t used = 0U;
    for (uint8_t i = 0U; i < detector->count; i++) {
        if (sample_ms - detector->times_ms[i] > detector->config.window_ms) {
            continue;
        }
        used++;
        for (uint8_t axis = 0U; axis < 3U; axis++) {
            int32_t value = detector->axes[i][axis];
            if (value < low[axis]) {
                low[axis] = value;
            }
            if (value > high[axis]) {
                high[axis] = value;
            }
        }
    }
    detector->span_mg = 0;
    if (used == 0U) {
        return;
    }
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        int64_t span = (int64_t)high[axis] - low[axis];
        if (span > detector->span_mg) {
            detector->span_mg = span;
        }
    }
    if (used >= detector->config.min_samples &&
        detector->span_mg >= detector->config.threshold_mg) {
        detector->has_motion = true;
        detector->motion_ms = sample_ms;
    }
}

bool accel_detect_is_recent(const accel_detect_t *detector, uint32_t now_ms)
{
    return detector->has_motion && now_ms - detector->motion_ms <= detector->config.hold_ms;
}
