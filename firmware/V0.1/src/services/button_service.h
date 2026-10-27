#ifndef APSLIM_BUTTON_SERVICE_H_
#define APSLIM_BUTTON_SERVICE_H_
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    uint32_t candidate_since_ms;
    uint32_t hold_ms;
    bool is_candidate_pressed;
    bool is_pressed;
    bool has_long_press_sent;
    int read_ret;
} button_service_t;

void button_service_init(button_service_t *button, uint32_t debounce_ms, uint32_t long_press_ms);
bool button_service_update(button_service_t *button, uint32_t now_ms, int read_ret,
                           bool is_raw_pressed);

#endif
