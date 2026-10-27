#include "button_service.h"

#include <string.h>

void button_service_init(button_service_t *button, uint32_t debounce_ms, uint32_t long_press_ms)
{
    memset(button, 0, sizeof(*button));
    button->debounce_ms = debounce_ms;
    button->long_press_ms = long_press_ms;
}

bool button_service_update(button_service_t *button, uint32_t now_ms, int read_ret,
                           bool is_raw_pressed)
{
    button->read_ret = read_ret;
    if (read_ret < 0) {
        button->is_candidate_pressed = false;
        button->is_pressed = false;
        button->hold_ms = 0U;
        /* A read error is not a confirmed release of an already consumed hold. */
        button->candidate_since_ms = now_ms;
        return false;
    }
    if (is_raw_pressed != button->is_candidate_pressed) {
        button->is_candidate_pressed = is_raw_pressed;
        button->candidate_since_ms = now_ms;
    }
    uint32_t elapsed_ms = now_ms - button->candidate_since_ms;
    if (elapsed_ms >= button->debounce_ms) {
        button->is_pressed = is_raw_pressed;
        if (!is_raw_pressed) {
            button->has_long_press_sent = false;
        }
    }
    /* Any sampled release cancels continuous qualification immediately. */
    button->hold_ms = button->is_pressed && is_raw_pressed ? elapsed_ms : 0U;
    if (button->is_pressed && is_raw_pressed && !button->has_long_press_sent &&
        button->hold_ms >= button->long_press_ms) {
        button->has_long_press_sent = true;
        return true;
    }
    return false;
}
