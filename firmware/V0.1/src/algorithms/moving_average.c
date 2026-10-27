#include "moving_average.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

int moving_average_init(moving_average_t *filter, uint8_t window)
{
    if (filter == NULL || window == 0U || window > MOVING_AVERAGE_MAX_SAMPLES) {
        return -EINVAL;
    }
    memset(filter, 0, sizeof(*filter));
    filter->window = window;
    return 0;
}

void moving_average_reset(moving_average_t *filter)
{
    uint8_t window = filter->window;
    memset(filter, 0, sizeof(*filter));
    filter->window = window;
}

int32_t moving_average_push(moving_average_t *filter, int32_t sample)
{
    if (filter->count == filter->window) {
        filter->sum -= filter->samples[filter->next];
    } else {
        filter->count++;
    }
    filter->samples[filter->next] = sample;
    filter->sum += sample;
    filter->next = (uint8_t)((filter->next + 1U) % filter->window);
    int64_t half = filter->count / 2U;
    return (int32_t)((filter->sum >= 0 ? filter->sum + half : filter->sum - half) / filter->count);
}
