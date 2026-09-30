#ifndef OCLOCK_BUTTON_H
#define OCLOCK_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OCLOCK_PRESS_NONE,
    OCLOCK_PRESS_SHORT,
    OCLOCK_PRESS_LONG,
} oclock_press_t;

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    bool armed;
    bool long_sent;
    int64_t raw_since_ms;
    int64_t pressed_since_ms;
} oclock_button_t;

void oclock_button_init(oclock_button_t *button, bool pressed, int64_t now_ms);
/* long_press_ms == 0 emits one short event on release regardless of hold time. */
oclock_press_t oclock_button_update(oclock_button_t *button, bool pressed,
                                   int64_t now_ms, int debounce_ms,
                                   int long_press_ms);
#endif
