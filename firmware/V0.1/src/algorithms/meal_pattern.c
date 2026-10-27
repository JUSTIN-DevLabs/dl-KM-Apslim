#include "meal_pattern.h"

#include <string.h>

void meal_pattern_init(meal_pattern_t *meal, const meal_pattern_config_t *config)
{
    memset(meal, 0, sizeof(*meal));
    meal->config = *config;
    meal->state = MEAL_STATE_WAIT_READY;
}

void meal_pattern_invalidate(meal_pattern_t *meal)
{
    meal->state = MEAL_STATE_ERROR;
    meal->is_pending = false;
    meal->has_sample = false;
    meal->has_become_ready = false;
}

void meal_pattern_pause(meal_pattern_t *meal)
{
    /* Range recovery never advances contact/release confirmation using old data. */
    meal->is_pending = false;
    meal->has_become_ready = false;
}

bool meal_pattern_update(meal_pattern_t *meal, uint32_t sample_ms, capacitive_detect_result_t cap,
                         bool has_recent_motion)
{
    meal->has_become_ready = false;
    if (meal->has_sample && meal->last_sample_ms == sample_ms) {
        return false;
    }
    meal->has_sample = true;
    meal->last_sample_ms = sample_ms;
    if (meal->state == MEAL_STATE_ERROR) {
        meal->state = MEAL_STATE_WAIT_READY;
    }
    bool condition;
    uint32_t duration_ms;
    if (meal->state == MEAL_STATE_READY) {
        condition = cap.is_contact && has_recent_motion;
        duration_ms = meal->config.contact_ms;
    } else if (meal->state == MEAL_STATE_EATING) {
        condition = cap.is_released;
        duration_ms = meal->config.release_ms;
    } else {
        condition = cap.is_released;
        duration_ms = meal->config.rearm_ms;
    }
    if (!condition) {
        meal->is_pending = false;
        return false;
    }
    if (!meal->is_pending) {
        meal->is_pending = true;
        meal->pending_since_ms = sample_ms;
        if (meal->state == MEAL_STATE_READY) {
            meal->candidate_contact_ms = sample_ms;
        }
    }
    if (sample_ms - meal->pending_since_ms < duration_ms) {
        return false;
    }
    meal->is_pending = false;
    if (meal->state == MEAL_STATE_READY) {
        meal->state = MEAL_STATE_EATING;
        meal->last_contact_ms = meal->candidate_contact_ms;
        meal->count++;
        return true;
    }
    if (meal->state == MEAL_STATE_EATING) {
        meal->state = MEAL_STATE_RETURN;
    } else {
        meal->state = MEAL_STATE_READY;
        meal->has_become_ready = true;
    }
    return false;
}

const char *meal_pattern_name(const meal_pattern_t *meal, bool has_recent_motion)
{
    switch (meal->state) {
    case MEAL_STATE_READY:
        return has_recent_motion ? "MOVE" : "READY";
    case MEAL_STATE_EATING:
        return "EATING";
    case MEAL_STATE_RETURN:
        return "RETURN";
    case MEAL_STATE_ERROR:
        return "ERROR";
    default:
        return "WAIT_READY";
    }
}
