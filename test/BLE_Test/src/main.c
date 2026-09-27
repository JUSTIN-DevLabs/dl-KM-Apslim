#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#define DEBUG_LED_NODE  DT_ALIAS(led1)
#define GREEN_LED_NODE  DT_ALIAS(led_green)

static const struct gpio_dt_spec debug_led =
    GPIO_DT_SPEC_GET(DEBUG_LED_NODE, gpios);

static const struct gpio_dt_spec green_led =
    GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios);

static const struct bt_data adv_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
};

static const struct bt_data scan_data[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE,
            CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int advertising_start(void)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                              adv_data, ARRAY_SIZE(adv_data),
                              scan_data, ARRAY_SIZE(scan_data));

    if (err != 0) {
        printk("BLE advertising failed: %d\n", err);
        return err;
    }

    printk("BLE advertising: %s\n", CONFIG_BT_DEVICE_NAME);
    return 0;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0) {
        printk("BLE connection failed: %u\n", err);
        return;
    }

    gpio_pin_set_dt(&green_led, 1);
    printk("BLE connected - Green LED ON\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    gpio_pin_set_dt(&green_led, 0);
    printk("BLE disconnected: 0x%02x - Green LED OFF\n", reason);
}

static void recycled(void)
{
    (void)advertising_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = recycled,
};

int main(void)
{
    int err;

    if (!gpio_is_ready_dt(&debug_led) ||
        !gpio_is_ready_dt(&green_led)) {
        printk("LED GPIO is not ready\n");
        return 0;
    }

    err = gpio_pin_configure_dt(&debug_led, GPIO_OUTPUT_INACTIVE);
    if (err != 0) {
        printk("Debug LED setup failed: %d\n", err);
        return 0;
    }

    err = gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
    if (err != 0) {
        printk("Green LED setup failed: %d\n", err);
        return 0;
    }

    err = bt_enable(NULL);
    if (err != 0) {
        printk("Bluetooth init failed: %d\n", err);
    } else {
        printk("Bluetooth initialized\n");
        (void)advertising_start();
    }

    /* BLE 연결 여부와 관계없이 0.5초마다 led1을 토글한다. */
    while (true) {
        err = gpio_pin_toggle_dt(&debug_led);
        if (err != 0) {
            printk("Debug LED toggle failed: %d\n", err);
        }

        k_sleep(K_MSEC(500));
    }
}