#ifndef APSLIM_LED_HAL_H_
#define APSLIM_LED_HAL_H_
#include <stdbool.h>

typedef enum {
    LED_RED,
    LED_GREEN,
    LED_BLUE,
    LED_DEBUG_1,
    LED_DEBUG_2,
    LED_COUNT,
} led_id_t;

int led_init(void);
int led_set(led_id_t id, bool is_on);
int led_get_status(led_id_t id);

#endif
