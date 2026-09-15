#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led0 =
    GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static const struct gpio_dt_spec led1 =
    GPIO_DT_SPEC_GET(LED1_NODE, gpios);

int main(void)
{
    int ret;

    /* GPIO device ready check */
    if (!gpio_is_ready_dt(&led0)) {
        return 0;
    }

    if (!gpio_is_ready_dt(&led1)) {
        return 0;
    }

    /* LED GPIO configuration */
    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return 0;
    }

    ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return 0;
    }

    while (1)
    {
        gpio_pin_set_dt(&led0, 1);
        gpio_pin_set_dt(&led1, 1);

        k_msleep(500);

        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);

        k_msleep(500);
    }

    return 0;
}