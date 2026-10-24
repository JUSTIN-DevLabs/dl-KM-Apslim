/**
 * @file main.c
 * @brief Start the reusable LIS2DH DK board validation application.
 */
#include <zephyr/sys/printk.h>

#include "accel_app.h"

int main(void)
{
    int ret = accel_app_init();
    if (ret < 0) {
        printk("[APP] Initialization failed (%d)\r\n", ret);
    }

    return ret;
}
