#ifndef APSLIM_DEVICE_CONTROL_H_
#define APSLIM_DEVICE_CONTROL_H_
#include <stdbool.h>
#include <stdint.h>

#include "meal_pattern.h"

int device_control_init(void);
void device_control_process(uint32_t now_ms, uint32_t elapsed_ticks);
/* Read-only state for diagnostics and host integration verification. */
bool device_control_is_on(void);
uint32_t device_control_meal_count(void);
meal_state_t device_control_meal_state(void);

#endif
