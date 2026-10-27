#ifndef APSLIM_MEAL_PATTERN_H_
#define APSLIM_MEAL_PATTERN_H_
#include <stdbool.h>
#include <stdint.h>

#include "capacitive_detect.h"

typedef enum {
    MEAL_STATE_WAIT_READY,
    MEAL_STATE_READY,
    MEAL_STATE_EATING,
    MEAL_STATE_RETURN,
    MEAL_STATE_ERROR,
} meal_state_t;
typedef struct {
    uint32_t contact_ms;
    uint32_t release_ms;
    uint32_t rearm_ms;
} meal_pattern_config_t;
typedef struct {
    meal_pattern_config_t config;
    meal_state_t state;
    uint32_t count;
    uint32_t pending_since_ms;
    uint32_t candidate_contact_ms;
    uint32_t last_contact_ms;
    uint32_t last_sample_ms;
    bool is_pending;
    bool has_sample;
    bool has_become_ready;
} meal_pattern_t;

void meal_pattern_init(meal_pattern_t *meal, const meal_pattern_config_t *config);
void meal_pattern_invalidate(meal_pattern_t *meal);
void meal_pattern_pause(meal_pattern_t *meal);
/* Returns true once when a new contact qualifies; count is a contact estimate. */
bool meal_pattern_update(meal_pattern_t *meal, uint32_t sample_ms, capacitive_detect_result_t cap,
                         bool has_recent_motion);
const char *meal_pattern_name(const meal_pattern_t *meal, bool has_recent_motion);

#endif
