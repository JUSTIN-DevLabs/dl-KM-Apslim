#include <stdint.h>

#include "app_timer.h"
#include "device_control.h"

int main(void)
{
    int ret = device_control_init();
    if (ret < 0) {
        return ret;
    }
    ret = app_timer_init();
    if (ret < 0) {
        return ret;
    }
    for (;;) {
        uint32_t now_ms;
        uint32_t elapsed_ticks;
        if (app_timer_wait(&now_ms, &elapsed_ticks) == 0) {
            device_control_process(now_ms, elapsed_ticks);
        }
    }
}
