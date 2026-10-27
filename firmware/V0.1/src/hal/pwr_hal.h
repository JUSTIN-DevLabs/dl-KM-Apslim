#ifndef APSLIM_PWR_HAL_H_
#define APSLIM_PWR_HAL_H_
#include <stdbool.h>

int pwr_init(void);
int pwr_enable_set(bool is_enabled);
int pwr_sw_state_read_raw(int *p_level);
bool pwr_sw_level_is_pressed(int level);
bool pwr_vbus_is_present(void);

#endif
