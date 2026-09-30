#include "oclock_album.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "oclock_album";
static const char *kAlbumMountPath = "/albumfs";
static const char *kAlbumPartitionLabel = "storage";
static const char *kAlbumMetaPath = "/albumfs/album.json";

typedef struct {
    uint8_t frame_count;
    uint16_t width;
    uint16_t height;
    uint16_t interval_ms;
} album_meta_t;

typedef struct {
    album_meta_t meta;
    uint8_t *frames[OCLOCK_ALBUM_MAX_FRAMES];
    uint8_t current_index;
    uint32_t version;
    int64_t last_advance_ms;
    bool mounted;
    bool valid;
} oclock_album_context_t;

static oclock_album_context_t g_album = {0};

static size_t frame_bytes(uint16_t width, uint16_t height)
{
    return ((size_t)width * (size_t)height + 7U) / 8U;
}

static void free_frames(void)
{
    for (size_t i = 0; i < OCLOCK_ALBUM_MAX_FRAMES; ++i) {
        free(g_album.frames[i]);
        g_album.frames[i] = NULL;
    }
    g_album.valid = false;
}

static esp_err_t ensure_spiffs(void)
{
    if (g_album.mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = kAlbumMountPath,
        .partition_label = kAlbumPartitionLabel,
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&conf), TAG, "spiffs register failed");
    g_album.mounted = true;
    return ESP_OK;
}

static esp_err_t load_album_from_fs(void)
{
    FILE *meta = fopen(kAlbumMetaPath, "rb");
    if (meta == NULL) {
        g_album.valid = false;
        return ESP_OK;
    }

    album_meta_t meta_buf = {0};
    if (fread(&meta_buf, sizeof(meta_buf), 1, meta) != 1) {
        fclose(meta);
        return ESP_FAIL;
    }
    fclose(meta);

    if (meta_buf.frame_count == 0 || meta_buf.frame_count > OCLOCK_ALBUM_MAX_FRAMES) {
        return ESP_FAIL;
    }

    free_frames();
    g_album.meta = meta_buf;
    size_t bytes = frame_bytes(meta_buf.width, meta_buf.height);
    for (uint8_t i = 0; i < meta_buf.frame_count; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "%s/frame_%u.bin", kAlbumMountPath, (unsigned)i);
        FILE *frame = fopen(path, "rb");
        if (frame == NULL) {
            free_frames();
            return ESP_FAIL;
        }
        g_album.frames[i] = (uint8_t *)malloc(bytes);
        if (g_album.frames[i] == NULL) {
            fclose(frame);
            free_frames();
            return ESP_ERR_NO_MEM;
        }
        if (fread(g_album.frames[i], bytes, 1, frame) != 1) {
            fclose(frame);
            free_frames();
            return ESP_FAIL;
        }
        fclose(frame);
    }

    g_album.current_index = 0;
    g_album.valid = true;
    return ESP_OK;
}

esp_err_t oclock_album_init(void)
{
    ESP_RETURN_ON_ERROR(ensure_spiffs(), TAG, "spiffs init failed");
    return load_album_from_fs();
}

esp_err_t oclock_album_tick(bool visible, int64_t now_ms)
{
    if (!visible || !g_album.valid || g_album.meta.frame_count <= 1) {
        return ESP_OK;
    }
    if ((now_ms - g_album.last_advance_ms) < g_album.meta.interval_ms) {
        return ESP_OK;
    }
    g_album.current_index = (uint8_t)((g_album.current_index + 1U) % g_album.meta.frame_count);
    g_album.last_advance_ms = now_ms;
    g_album.version++;
    return ESP_OK;
}

esp_err_t oclock_album_store_from_json(const char *json_text)
{
    ESP_RETURN_ON_ERROR(ensure_spiffs(), TAG, "spiffs init failed");
    ESP_RETURN_ON_FALSE(json_text != NULL, ESP_ERR_INVALID_ARG, TAG, "json text is null");

    cJSON *root = cJSON_Parse(json_text);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_FAIL, TAG, "album json parse failed");

    cJSON *width = cJSON_GetObjectItemCaseSensitive(root, "width");
    cJSON *height = cJSON_GetObjectItemCaseSensitive(root, "height");
    cJSON *interval = cJSON_GetObjectItemCaseSensitive(root, "interval_ms");
    cJSON *frames = cJSON_GetObjectItemCaseSensitive(root, "frames");
    ESP_RETURN_ON_FALSE(cJSON_IsNumber(width) && cJSON_IsNumber(height) && cJSON_IsArray(frames),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "album json missing fields");

    album_meta_t meta = {
        .frame_count = (uint8_t)cJSON_GetArraySize(frames),
        .width = (uint16_t)width->valueint,
        .height = (uint16_t)height->valueint,
        .interval_ms = (uint16_t)(cJSON_IsNumber(interval) ? interval->valueint : 1200),
    };
    ESP_RETURN_ON_FALSE(meta.frame_count > 0 && meta.frame_count <= OCLOCK_ALBUM_MAX_FRAMES,
                        ESP_ERR_INVALID_SIZE,
                        TAG,
                        "invalid frame count");

    size_t bytes = frame_bytes(meta.width, meta.height);
    free_frames();
    g_album.meta = meta;

    for (uint8_t i = 0; i < meta.frame_count; ++i) {
        cJSON *item = cJSON_GetArrayItem(frames, i);
        ESP_RETURN_ON_FALSE(cJSON_IsString(item) && item->valuestring != NULL,
                            ESP_ERR_INVALID_ARG,
                            TAG,
                            "frame must be string");
        size_t src_len = strlen(item->valuestring);
        ESP_RETURN_ON_FALSE(src_len == bytes * 2U, ESP_ERR_INVALID_SIZE, TAG, "frame hex size mismatch");
        g_album.frames[i] = (uint8_t *)malloc(bytes);
        ESP_RETURN_ON_FALSE(g_album.frames[i] != NULL, ESP_ERR_NO_MEM, TAG, "frame alloc failed");
        for (size_t j = 0; j < bytes; ++j) {
            unsigned int value = 0;
            sscanf(item->valuestring + j * 2U, "%2x", &value);
            g_album.frames[i][j] = (uint8_t)value;
        }
    }

    FILE *meta_file = fopen(kAlbumMetaPath, "wb");
    ESP_RETURN_ON_FALSE(meta_file != NULL, ESP_FAIL, TAG, "meta open failed");
    fwrite(&meta, sizeof(meta), 1, meta_file);
    fclose(meta_file);

    for (uint8_t i = 0; i < meta.frame_count; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "%s/frame_%u.bin", kAlbumMountPath, (unsigned)i);
        FILE *frame = fopen(path, "wb");
        ESP_RETURN_ON_FALSE(frame != NULL, ESP_FAIL, TAG, "frame open failed");
        fwrite(g_album.frames[i], bytes, 1, frame);
        fclose(frame);
    }

    g_album.current_index = 0;
    g_album.last_advance_ms = 0;
    g_album.valid = true;
    g_album.version++;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t oclock_album_clear(void)
{
    free_frames();
    remove(kAlbumMetaPath);
    for (uint8_t i = 0; i < OCLOCK_ALBUM_MAX_FRAMES; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "%s/frame_%u.bin", kAlbumMountPath, (unsigned)i);
        remove(path);
    }
    memset(&g_album.meta, 0, sizeof(g_album.meta));
    g_album.current_index = 0;
    g_album.version++;
    return ESP_OK;
}

void oclock_album_build_view(oclock_album_view_t *out_view)
{
    memset(out_view, 0, sizeof(*out_view));
    if (!g_album.valid) {
        return;
    }
    out_view->frame_data = g_album.frames[g_album.current_index];
    out_view->width = g_album.meta.width;
    out_view->height = g_album.meta.height;
    out_view->interval_ms = g_album.meta.interval_ms;
    out_view->frame_count = g_album.meta.frame_count;
    out_view->current_index = g_album.current_index;
    out_view->valid = true;
}

uint32_t oclock_album_get_version(void)
{
    return g_album.version;
}
