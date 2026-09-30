#ifndef OCLOCK_BOARD_H
#define OCLOCK_BOARD_H

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    OCLOCK_BUTTON_EVENT_NONE = 0,
    OCLOCK_BUTTON_EVENT_SHORT_PRESS,
    OCLOCK_BUTTON_EVENT_LONG_PRESS,
    OCLOCK_BUTTON_EVENT_AUDIO_TEST = 4,
} oclock_button_event_t;

esp_err_t oclock_board_init(void);
int64_t oclock_board_now_ms(void);
oclock_button_event_t oclock_board_poll_button(void);

#endif
