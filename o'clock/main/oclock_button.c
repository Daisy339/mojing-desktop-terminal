#include "oclock_button.h"

void oclock_button_init(oclock_button_t *button, bool pressed, int64_t now_ms)
{
    *button = (oclock_button_t){
        .raw_pressed = pressed,
        .stable_pressed = pressed,
        .armed = !pressed,
        .raw_since_ms = now_ms,
        .pressed_since_ms = now_ms,
    };
}

oclock_press_t oclock_button_update(oclock_button_t *button, bool pressed,
                                   int64_t now_ms, int debounce_ms,
                                   int long_press_ms)
{
    if (pressed != button->raw_pressed) {
        button->raw_pressed = pressed;
        button->raw_since_ms = now_ms;
    }
    if (button->stable_pressed != button->raw_pressed &&
        now_ms - button->raw_since_ms >= debounce_ms) {
        button->stable_pressed = button->raw_pressed;
        if (button->stable_pressed) {
            button->pressed_since_ms = button->raw_since_ms;
            button->long_sent = false;
        } else {
            bool emit = button->armed && !button->long_sent;
            button->armed = true;
            return emit ? OCLOCK_PRESS_SHORT : OCLOCK_PRESS_NONE;
        }
    }
    if (button->armed && button->stable_pressed && button->raw_pressed &&
        !button->long_sent && long_press_ms > 0 &&
        now_ms - button->pressed_since_ms >= long_press_ms) {
        button->long_sent = true;
        return OCLOCK_PRESS_LONG;
    }
    return OCLOCK_PRESS_NONE;
}
