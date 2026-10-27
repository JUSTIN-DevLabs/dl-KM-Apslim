#include "app_timer.h"

#include <errno.h>

#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "app_config.h"

static const struct device *const s_p_timer = DEVICE_DT_GET(DT_NODELABEL(timer2));
K_SEM_DEFINE(s_tick_sem, 0, 1);
static atomic_t s_ticks;
static uint32_t s_previous_ticks;

/* Private function prototypes */
static void app_timer_isr_handler(const struct device *dev, void *user_data);

/* Public functions */
int app_timer_init(void)
{
    if (!device_is_ready(s_p_timer)) {
        return -ENODEV;
    }
    atomic_set(&s_ticks, 0);
    s_previous_ticks = 0U;
    struct counter_top_cfg config = {
        .ticks = counter_us_to_ticks(s_p_timer, APP_TIMER_TICK_MS * 1000ULL),
        .callback = app_timer_isr_handler,
        .user_data = NULL,
        .flags = 0U,
    };
    if (config.ticks == 0U || config.ticks > counter_get_max_top_value(s_p_timer)) {
        return -EINVAL;
    }
    int ret = counter_set_top_value(s_p_timer, &config);
    return ret == 0 ? counter_start(s_p_timer) : ret;
}

int app_timer_wait(uint32_t *p_now_ms, uint32_t *p_elapsed_ticks)
{
    int ret = k_sem_take(&s_tick_sem, K_FOREVER);
    if (ret < 0) {
        return ret;
    }
    uint32_t ticks = (uint32_t)atomic_get(&s_ticks);
    *p_elapsed_ticks = ticks - s_previous_ticks;
    *p_now_ms = ticks * APP_TIMER_TICK_MS;
    s_previous_ticks = ticks;
    return 0;
}

/* Private functions */
static void app_timer_isr_handler(const struct device *dev, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    /* ISR only signals elapsed time. All I2C/ADC/formatting runs in main. */
    atomic_inc(&s_ticks);
    k_sem_give(&s_tick_sem);
}
