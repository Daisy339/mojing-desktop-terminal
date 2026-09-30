#include "oclock_board.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "oclock_board_config.h"
#include "oclock_button.h"

static const char *TAG = "oclock_board";

static oclock_button_t g_key;
static oclock_button_t g_boot;

static bool button_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == OCLOCK_BUTTON_ACTIVE_LEVEL;
}

esp_err_t oclock_board_init(void)
{
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << OCLOCK_BUTTON_PRIMARY_GPIO) |
                        (1ULL << OCLOCK_BUTTON_FALLBACK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&gpio_conf), TAG, "button gpio init failed");

    oclock_button_init(&g_key, button_pressed(OCLOCK_BUTTON_PRIMARY_GPIO),
                       oclock_board_now_ms());
    oclock_button_init(&g_boot, button_pressed(OCLOCK_BUTTON_FALLBACK_GPIO),
                       oclock_board_now_ms());
    ESP_LOGI(TAG,
             "Board input initialized. Page key on GPIO %d, audio test BOOT "
             "on GPIO %d.",
             OCLOCK_BUTTON_PRIMARY_GPIO,
             OCLOCK_BUTTON_FALLBACK_GPIO);
    return ESP_OK;
}

int64_t oclock_board_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

oclock_button_event_t oclock_board_poll_button(void)
{
    const int64_t now_ms = oclock_board_now_ms();
    const oclock_press_t key = oclock_button_update(
        &g_key, button_pressed(OCLOCK_BUTTON_PRIMARY_GPIO), now_ms,
        OCLOCK_BUTTON_DEBOUNCE_MS, OCLOCK_BUTTON_LONG_PRESS_MS);
    const oclock_press_t boot = oclock_button_update(
        &g_boot, button_pressed(OCLOCK_BUTTON_FALLBACK_GPIO), now_ms,
        OCLOCK_BUTTON_DEBOUNCE_MS, 0);
    if (boot == OCLOCK_PRESS_SHORT) return OCLOCK_BUTTON_EVENT_AUDIO_TEST;
    if (key == OCLOCK_PRESS_LONG) return OCLOCK_BUTTON_EVENT_LONG_PRESS;
    if (key == OCLOCK_PRESS_SHORT) return OCLOCK_BUTTON_EVENT_SHORT_PRESS;
    return OCLOCK_BUTTON_EVENT_NONE;
}
