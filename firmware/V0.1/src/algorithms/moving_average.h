#ifndef APSLIM_MOVING_AVERAGE_H_
#define APSLIM_MOVING_AVERAGE_H_
#include <stdint.h>

#define MOVING_AVERAGE_MAX_SAMPLES 32U
typedef struct {
    int32_t samples[MOVING_AVERAGE_MAX_SAMPLES];
    int64_t sum;
    uint8_t window;
    uint8_t count;
    uint8_t next;
} moving_average_t;

int moving_average_init(moving_average_t *filter, uint8_t window);
/** Non-NULL, successfully initialized filter required; preserves its window size. */
void moving_average_reset(moving_average_t *filter);
/** Push one distinct valid sample; returns rounded mean. Non-NULL initialized filter required.
 * Uses the available sample count during warm-up; no zero padding. Single caller only.
 */
int32_t moving_average_push(moving_average_t *filter, int32_t sample);

#endif
