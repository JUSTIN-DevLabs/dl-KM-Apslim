#include "capacitive_detect.h"

capacitive_detect_result_t capacitive_detect_evaluate(const capacitive_detect_config_t *config,
                                                      int32_t filtered_pf_x10000)
{
    int64_t delta = (int64_t)filtered_pf_x10000 - config->baseline_pf_x10000;
    capacitive_detect_result_t result = {
        .delta_pf_x10000 = (int32_t)delta,
        .is_contact = delta >= config->on_delta_pf_x10000,
        .is_released = delta <= config->off_delta_pf_x10000,
    };
    return result;
}
