#ifndef OCLOCK_RENDERER_H
#define OCLOCK_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "oclock_page_scheduler.h"
#include "oclock_environment.h"
#include "oclock_album.h"
#include "oclock_todo.h"
#include "oclock_time_view.h"
#include "oclock_weather.h"
#include "oclock_audio.h"

typedef struct {
    oclock_album_view_t album_view;
    oclock_page_scheduler_snapshot_t scheduler;
    oclock_environment_view_t environment_view;
    oclock_todo_view_t todo_view;
    oclock_time_view_t time_view;
    oclock_weather_view_t weather_view;
    oclock_audio_status_t audio_status;
    int64_t now_ms;
    uint32_t render_count;
} oclock_render_model_t;

esp_err_t oclock_renderer_init(void);
void oclock_renderer_render(const oclock_render_model_t *model);

#endif
