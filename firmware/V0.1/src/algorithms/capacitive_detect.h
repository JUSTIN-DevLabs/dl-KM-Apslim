#ifndef APSLIM_CAPACITIVE_DETECT_H_
#define APSLIM_CAPACITIVE_DETECT_H_
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t baseline_pf_x10000;
    int32_t on_delta_pf_x10000;
    int32_t off_delta_pf_x10000;
} capacitive_detect_config_t;
typedef struct {
    int32_t delta_pf_x10000;
    bool is_contact;
    bool is_released;
} capacitive_detect_result_t;

capacitive_detect_result_t capacitive_detect_evaluate(const capacitive_detect_config_t *config,
                                                      int32_t filtered_pf_x10000);

#endif
