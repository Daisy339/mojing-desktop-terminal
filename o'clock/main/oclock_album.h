#ifndef OCLOCK_ALBUM_H
#define OCLOCK_ALBUM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCLOCK_ALBUM_MAX_FRAMES 8
#define OCLOCK_ALBUM_FRAME_WIDTH 400
#define OCLOCK_ALBUM_FRAME_HEIGHT 300

typedef struct {
    const uint8_t *frame_data;
    uint16_t width;
    uint16_t height;
    uint16_t interval_ms;
    uint8_t frame_count;
    uint8_t current_index;
    bool valid;
} oclock_album_view_t;

esp_err_t oclock_album_init(void);
esp_err_t oclock_album_tick(bool visible, int64_t now_ms);
esp_err_t oclock_album_store_from_json(const char *json_text);
esp_err_t oclock_album_clear(void);
void oclock_album_build_view(oclock_album_view_t *out_view);
uint32_t oclock_album_get_version(void);

#ifdef __cplusplus
}
#endif

#endif
