#include "led_hal.h"

#include <errno.h>

#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec s_leds[LED_COUNT] = {
    [LED_RED] = GPIO_DT_SPEC_GET(DT_ALIAS(led_r), gpios),
    [LED_GREEN] = GPIO_DT_SPEC_GET(DT_ALIAS(led_g), gpios),
    [LED_BLUE] = GPIO_DT_SPEC_GET(DT_ALIAS(led_b), gpios),
    [LED_DEBUG_1] = GPIO_DT_SPEC_GET(DT_ALIAS(debug_led_1), gpios),
    [LED_DEBUG_2] = GPIO_DT_SPEC_GET(DT_ALIAS(debug_led_2), gpios),
};
static int s_status[LED_COUNT];
static bool s_is_on[LED_COUNT];

int led_init(void)
{
    int first_error = 0;
    for (int i = 0; i < LED_COUNT; i++) {
        s_is_on[i] = false;
        s_status[i] = gpio_is_ready_dt(&s_leds[i])
                          ? gpio_pin_configure_dt(&s_leds[i], GPIO_OUTPUT_INACTIVE)
                          : -ENODEV;
        if (first_error == 0 && s_status[i] < 0) {
            first_error = s_status[i];
        }
    }
    return first_error;
}

int led_set(led_id_t id, bool is_on)
{
    if ((unsigned int)id >= LED_COUNT) {
        return -EINVAL;
    }
    if (s_status[id] < 0) {
        return s_status[id];
    }
    if (s_is_on[id] == is_on) {
        return 0;
    }
    /* Logical ON; gpio_dt_spec applies GPIO_ACTIVE_LOW from the overlay. */
    int ret = gpio_pin_set_dt(&s_leds[id], is_on ? 1 : 0);
    if (ret == 0) {
        s_is_on[id] = is_on;
    }
    s_status[id] = ret;
    return ret;
}

int led_get_status(led_id_t id)
{
    return (unsigned int)id < LED_COUNT ? s_status[id] : -EINVAL;
}
