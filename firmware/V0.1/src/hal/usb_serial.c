#include "usb_serial.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>

#include "app_config.h"

static const struct device *const s_p_usb = DEVICE_DT_GET(DT_NODELABEL(usb_cdc));
static uint8_t s_storage[APP_USB_TX_BUFFER_BYTES];
static struct ring_buf s_ring;
static bool s_is_connected;
static int s_init_ret;

/* Private function prototypes */
static void usb_uart_callback(const struct device *dev, void *user_data);

/* Public functions */
int usb_serial_init(void)
{
    s_is_connected = false;
    ring_buf_init(&s_ring, sizeof(s_storage), s_storage);
    if (!device_is_ready(s_p_usb)) {
        s_init_ret = -ENODEV;
        return s_init_ret;
    }
    int ret = uart_irq_callback_user_data_set(s_p_usb, usb_uart_callback, NULL);
    if (ret == 0) {
        ret = usb_enable(NULL);
    }
    if (ret == -EALREADY) {
        ret = 0;
    }
    if (ret == 0) {
        uart_irq_rx_enable(s_p_usb);
    }
    s_init_ret = ret;
    return ret;
}

bool usb_serial_process(void)
{
    if (s_init_ret < 0) {
        return false;
    }
    uint32_t dtr = 0U;
    bool next = uart_line_ctrl_get(s_p_usb, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0U;
    if (next == s_is_connected) {
        return false;
    }
    unsigned int key = irq_lock();
    uart_irq_tx_disable(s_p_usb);
    ring_buf_reset(&s_ring);
    s_is_connected = next;
    irq_unlock(key);
    return true;
}

bool usb_serial_is_connected(void)
{
    return s_is_connected;
}

bool usb_serial_write(const char *text, size_t length)
{
    if (!s_is_connected || length > APP_USB_TX_BUFFER_BYTES ||
        ring_buf_space_get(&s_ring) < length) {
        return false;
    }
    uint32_t written = ring_buf_put(&s_ring, (const uint8_t *)text, (uint32_t)length);
    if (written != length) {
        return false;
    }
    uart_irq_tx_enable(s_p_usb);
    return true;
}

/* Private functions */
static void usb_uart_callback(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    for (unsigned int i = 0U; i < 8U; i++) {
        if (!uart_irq_update(dev) || !uart_irq_is_pending(dev)) {
            break;
        }
        if (uart_irq_rx_ready(dev)) {
            uint8_t ignored[64];
            if (uart_fifo_read(dev, ignored, sizeof(ignored)) < 0) {
                uart_irq_rx_disable(dev);
            }
        }
        if (uart_irq_tx_ready(dev)) {
            uint8_t *data = NULL;
            uint32_t length = ring_buf_get_claim(&s_ring, &data, 128U);
            if (length == 0U) {
                uart_irq_tx_disable(dev);
                break;
            }
            int written = uart_fifo_fill(dev, data, (int)length);
            uint32_t used = written > 0 ? (uint32_t)written : 0U;
            int ret = ring_buf_get_finish(&s_ring, used);
            if (ret < 0 || written < 0) {
                uart_irq_tx_disable(dev);
                break;
            }
            if (written == 0) {
                break;
            }
        }
    }
}
