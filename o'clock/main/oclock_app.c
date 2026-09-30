#include "oclock_app.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "oclock_board.h"
#include "oclock_page_scheduler.h"
#include "oclock_album.h"
#include "oclock_renderer.h"
#include "oclock_environment.h"
#include "oclock_time.h"
#include "oclock_time_view.h"
#include "oclock_todo.h"
#include "oclock_weather.h"
#include "oclock_network.h"
#include "oclock_web.h"
#include "oclock_audio.h"

typedef struct {
    oclock_page_scheduler_t scheduler;
    uint32_t render_count;
    int last_rendered_minute;
    int last_rendered_second;
    uint32_t last_album_version;
    uint32_t last_environment_version;
    uint32_t last_todo_version;
    uint32_t last_weather_version;
    oclock_network_status_t last_network_status;
    bool has_network_status;
    uint32_t last_audio_version;
} oclock_app_context_t;

static const char *TAG = "oclock_app";

static esp_err_t oclock_app_init(oclock_app_context_t *context)
{
    const int64_t now_ms = oclock_board_now_ms();

    ESP_RETURN_ON_ERROR(oclock_board_init(), TAG, "board init failed");
    ESP_ERROR_CHECK_WITHOUT_ABORT(oclock_audio_init());
    ESP_RETURN_ON_ERROR(oclock_time_init(), TAG, "time init failed");
    ESP_RETURN_ON_ERROR(oclock_renderer_init(), TAG, "renderer init failed");
    ESP_RETURN_ON_ERROR(oclock_network_init(), TAG, "network init failed");
    ESP_RETURN_ON_ERROR(oclock_network_start(), TAG, "network start failed");
    ESP_RETURN_ON_ERROR(oclock_album_init(), TAG, "album init failed");
    ESP_RETURN_ON_ERROR(oclock_environment_init(), TAG, "environment init failed");
    ESP_RETURN_ON_ERROR(oclock_todo_init(), TAG, "todo init failed");
    ESP_RETURN_ON_ERROR(oclock_weather_init(), TAG, "weather init failed");
    ESP_RETURN_ON_ERROR(oclock_web_start(), TAG, "web start failed");

    oclock_page_scheduler_init(&context->scheduler, now_ms);
    context->render_count = 0;
    context->last_rendered_minute = -1;
    context->last_rendered_second = -1;
    context->last_album_version = 0;
    context->last_environment_version = 0;
    context->last_todo_version = 0;
    context->last_weather_version = 0;
    context->has_network_status = false;
    context->last_audio_version = 0;
    return ESP_OK;
}

static bool oclock_app_handle_button(oclock_app_context_t *context,
                                     int64_t now_ms)
{
    const oclock_button_event_t button_event = oclock_board_poll_button();

    switch (button_event) {
    case OCLOCK_BUTTON_EVENT_SHORT_PRESS:
        ESP_LOGI(TAG, "Short press received");
        return oclock_page_scheduler_on_short_press(&context->scheduler,
                                                    now_ms);
    case OCLOCK_BUTTON_EVENT_LONG_PRESS:
        ESP_LOGI(TAG, "Long press received");
        return oclock_page_scheduler_on_long_press(&context->scheduler,
                                                   now_ms);
    case OCLOCK_BUTTON_EVENT_AUDIO_TEST:
        if (oclock_audio_is_busy()) {
            ESP_LOGI(TAG, "BOOT button cancels audio self-test");
            oclock_audio_cancel();
        } else {
            ESP_LOGI(TAG, "BOOT button audio self-test requested");
            ESP_ERROR_CHECK_WITHOUT_ABORT(oclock_audio_start_self_test());
        }
        return false;
    case OCLOCK_BUTTON_EVENT_NONE:
    default:
        return false;
    }
}

static void oclock_app_render(oclock_app_context_t *context, int64_t now_ms)
{
    oclock_render_model_t model = {0};
    oclock_datetime_t current_time = {0};

    oclock_page_scheduler_get_snapshot(&context->scheduler,
                                       now_ms,
                                       &model.scheduler);
    oclock_audio_get_status(&model.audio_status);
    oclock_album_build_view(&model.album_view);
    if (oclock_time_get_current(&current_time) == ESP_OK) {
        oclock_time_build_view(&current_time, &model.time_view);
        oclock_environment_build_view(&current_time, &model.environment_view);
        oclock_todo_build_view(&current_time, &model.todo_view);
        oclock_weather_build_view(&current_time, &model.weather_view);
    } else {
        oclock_time_build_view(NULL, &model.time_view);
        oclock_environment_build_view(NULL, &model.environment_view);
        oclock_todo_build_view(NULL, &model.todo_view);
        oclock_weather_build_view(NULL, &model.weather_view);
    }
    model.now_ms = now_ms;
    model.render_count = ++context->render_count;

    if (oclock_page_scheduler_take_refresh_request(&context->scheduler)) {
        ESP_LOGI(TAG,
                 "Placeholder refresh action accepted. Weather, sensor, and "
                 "todo refresh hooks will be connected in later slices.");
        ESP_ERROR_CHECK_WITHOUT_ABORT(oclock_weather_request_refresh());
    }

    oclock_renderer_render(&model);
}

void oclock_app_main(void)
{
    oclock_app_context_t context = {0};
    bool render_dirty = true;

    ESP_LOGI(TAG, "Starting O'Clock page shell");

    if (oclock_app_init(&context) != ESP_OK) {
        ESP_LOGE(TAG, "Application init failed");
        return;
    }

    while (true) {
        const int64_t now_ms = oclock_board_now_ms();
        oclock_datetime_t current_time = {0};
        oclock_network_status_t net_status = {0};
        bool network_status_changed = false;

        render_dirty |= oclock_app_handle_button(&context, now_ms);
        render_dirty |= oclock_page_scheduler_tick(&context.scheduler, now_ms);
        (void)oclock_network_tick();
        (void)oclock_album_tick(context.scheduler.current_page == OCLOCK_PAGE_ALBUM, now_ms);
        (void)oclock_environment_tick();
        (void)oclock_weather_tick();
        if (oclock_time_get_current(&current_time) == ESP_OK) {
            if (context.scheduler.current_page == OCLOCK_PAGE_HOME) {
                if (current_time.second != context.last_rendered_second) {
                    context.last_rendered_second = current_time.second;
                    context.last_rendered_minute = current_time.minute;
                    render_dirty = true;
                }
            } else if (current_time.minute != context.last_rendered_minute) {
                context.last_rendered_minute = current_time.minute;
                context.last_rendered_second = current_time.second;
                render_dirty = true;
            }
        }
        if (oclock_todo_get_version() != context.last_todo_version) {
            context.last_todo_version = oclock_todo_get_version();
            render_dirty = true;
        }
        if (oclock_environment_get_version() != context.last_environment_version) {
            context.last_environment_version = oclock_environment_get_version();
            render_dirty = true;
        }
        if (oclock_album_get_version() != context.last_album_version) {
            context.last_album_version = oclock_album_get_version();
            render_dirty = true;
        }
        if (oclock_weather_get_version() != context.last_weather_version) {
            context.last_weather_version = oclock_weather_get_version();
            render_dirty = true;
        }
        if (oclock_network_get_status(&net_status) == ESP_OK) {
            network_status_changed =
                !context.has_network_status ||
                memcmp(&context.last_network_status, &net_status, sizeof(net_status)) != 0;
            if (network_status_changed) {
                context.last_network_status = net_status;
                context.has_network_status = true;
            }
        }
        render_dirty |= network_status_changed;
        oclock_audio_status_t audio_status = {0};
        oclock_audio_get_status(&audio_status);
        if (audio_status.version != context.last_audio_version) {
            context.last_audio_version = audio_status.version;
            render_dirty = true;
        }

        if (render_dirty) {
            oclock_app_render(&context, now_ms);
            render_dirty = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
