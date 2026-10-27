#ifndef APSLIM_APP_TIMER_H_
#define APSLIM_APP_TIMER_H_
#include <stdint.h>
int app_timer_init(void);
int app_timer_wait(uint32_t *p_now_ms, uint32_t *p_elapsed_ticks);
#endif
