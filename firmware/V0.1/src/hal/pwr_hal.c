#include "pwr_hal.h"

#include <errno.h>

#include <hal/nrf_power.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec s_hold = GPIO_DT_SPEC_GET(DT_ALIAS(power_hold), gpios);
static const struct gpio_dt_spec s_button = GPIO_DT_SPEC_GET(DT_ALIAS(power_button), gpios);

int pwr_init(void)
{
    if (!gpio_is_ready_dt(&s_hold) || !gpio_is_ready_dt(&s_button)) {
        return -ENODEV;
    }
    /* User-supplied TIMER2 source: LOW until the first 3-second hold qualifies. */
    int ret = gpio_pin_configure_dt(&s_hold, GPIO_OUTPUT_INACTIVE);
    if (ret == 0) {
        ret = gpio_pin_configure_dt(&s_button, GPIO_INPUT);
    }
    return ret;
}

int pwr_enable_set(bool is_enabled)
{
    return gpio_pin_set_dt(&s_hold, is_enabled ? 1 : 0);
}

int pwr_sw_state_read_raw(int *p_level)
{
    if (p_level == NULL) {
        return -EINVAL;
    }
    int level = gpio_pin_get_raw(s_button.port, s_button.pin);
    if (level < 0) {
        return level;
    }
    *p_level = level;
    return 0;
}

bool pwr_sw_level_is_pressed(int level)
{
    return (s_button.dt_flags & GPIO_ACTIVE_LOW) != 0U ? level == 0 : level != 0;
}

bool pwr_vbus_is_present(void)
{
    /* Read hardware VBUS status; independent of host DTR and serial monitor. */
    return nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
}
