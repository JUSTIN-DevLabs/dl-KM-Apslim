#ifndef APSLIM_ACCEL_DETECT_H_
#define APSLIM_ACCEL_DETECT_H_
#include <stdbool.h>
#include <stdint.h>

#define ACCEL_DETECT_HISTORY 32U
typedef struct {
    uint32_t window_ms;
    uint32_t hold_ms;
    int32_t threshold_mg;
    uint8_t min_samples;
} accel_detect_config_t;
typedef struct {
    accel_detect_config_t config;
    int32_t axes[ACCEL_DETECT_HISTORY][3];
    uint32_t times_ms[ACCEL_DETECT_HISTORY];
    uint8_t next;
    uint8_t count;
    int64_t span_mg;
    uint32_t motion_ms;
    bool has_motion;
} accel_detect_t;

void accel_detect_init(accel_detect_t *detector, const accel_detect_config_t *config);
void accel_detect_reset(accel_detect_t *detector);
void accel_detect_update(accel_detect_t *detector, uint32_t sample_ms, const int32_t axes_mg[3]);
bool accel_detect_is_recent(const accel_detect_t *detector, uint32_t now_ms);

#endif
