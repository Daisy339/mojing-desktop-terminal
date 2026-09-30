#include "oclock_renderer.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "u8g2.h"
#include "u8x8.h"

#include "oclock_board_config.h"
#include "oclock_pages.h"
#include "oclock_rlcd_port.h"

static const char *TAG = "oclock_renderer";

typedef struct {
    u8g2_t u8g2;
    uint8_t *buffer;
    bool ready;
} oclock_canvas_t;

static oclock_canvas_t g_canvas = {0};

static const u8x8_display_info_t g_display_info = {
    .chip_enable_level = 0,
    .chip_disable_level = 1,
    .post_chip_enable_wait_ns = 0,
    .pre_chip_disable_wait_ns = 0,
    .reset_pulse_width_ms = 20,
    .post_reset_wait_ms = 50,
    .sda_setup_time_ns = 0,
    .sck_pulse_width_ns = 0,
    .sck_clock_hz = 24000000,
    .i2c_bus_clock_100kHz = 4,
    .data_setup_time_ns = 0,
    .write_pulse_width_ns = 0,
    .tile_width = 38,
    .tile_height = 50,
    .default_x_offset = 0,
    .flipmode_x_offset = 0,
    .pixel_width = 300,
    .pixel_height = 400,
};

static uint8_t oclock_virtual_byte_cb(u8x8_t *u8x8,
                                      uint8_t msg,
                                      uint8_t arg_int,
                                      void *arg_ptr)
{
    (void)u8x8;
    (void)msg;
    (void)arg_int;
    (void)arg_ptr;
    return 1;
}

static uint8_t oclock_virtual_gpio_and_delay_cb(u8x8_t *u8x8,
                                                uint8_t msg,
                                                uint8_t arg_int,
                                                void *arg_ptr)
{
    (void)u8x8;
    (void)msg;
    (void)arg_int;
    (void)arg_ptr;
    return 1;
}

static uint8_t oclock_virtual_display_cb(u8x8_t *u8x8,
                                         uint8_t msg,
                                         uint8_t arg_int,
                                         void *arg_ptr)
{
    (void)arg_int;
    (void)arg_ptr;

    switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
        u8x8_d_helper_display_setup_memory(u8x8, &g_display_info);
        return 1;
    case U8X8_MSG_DISPLAY_INIT:
    case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
    case U8X8_MSG_DISPLAY_REFRESH:
    case U8X8_MSG_DISPLAY_DRAW_TILE:
        return 1;
    default:
        return 0;
    }
}

static void oclock_renderer_log_page(const oclock_render_model_t *model)
{
    const oclock_page_id_t page = model->scheduler.current_page;

    ESP_LOGI(TAG,
             "\n=== O'Clock Page Shell ===\n"
             "Render: %" PRIu32 "\n"
             "Page: %s\n"
             "Copy: %s\n"
             "Auto rotation paused: %s\n"
             "Pause remaining: %" PRId64 " ms\n"
             "Refresh feedback: %s\n"
             "Refresh remaining: %" PRId64 " ms\n"
             "==========================",
             model->render_count,
             oclock_page_name(page),
             oclock_page_placeholder_copy(page),
             model->scheduler.auto_rotation_paused ? "yes" : "no",
             model->scheduler.manual_pause_remaining_ms,
             model->scheduler.refresh_feedback_active ? "visible" : "idle",
             model->scheduler.refresh_feedback_remaining_ms);
}

static void oclock_renderer_draw_page_frame(u8g2_t *u8g2,
                                            const oclock_render_model_t *model)
{
    char page_badge[48];

    u8g2_DrawFrame(u8g2, 4, 4, OCLOCK_LCD_WIDTH - 8, OCLOCK_LCD_HEIGHT - 8);
    u8g2_DrawRFrame(u8g2, 10, 10, OCLOCK_LCD_WIDTH - 20, 46, 4);

    u8g2_SetFont(u8g2, u8g2_font_helvB18_tr);
    u8g2_DrawStr(u8g2, 20, 40, oclock_page_name(model->scheduler.current_page));

    snprintf(page_badge,
             sizeof(page_badge),
             "Page %d/%d",
             (int)model->scheduler.current_page + 1,
             OCLOCK_PAGE_COUNT);
    u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
    u8g2_DrawStr(u8g2, OCLOCK_LCD_WIDTH - 92, 40, page_badge);
}

static void oclock_renderer_extract_temp_value(const char *source,
                                               char *out,
                                               size_t out_size)
{
    size_t write_index = 0;
    bool started = false;

    if (out_size == 0) {
        return;
    }

    if (source != NULL) {
        for (size_t i = 0; source[i] != '\0' && write_index + 1 < out_size; ++i) {
            const char ch = source[i];
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-') {
                out[write_index++] = ch;
                started = true;
            } else if (started) {
                break;
            }
        }
    }

    if (write_index == 0) {
        snprintf(out, out_size, "--.-");
        return;
    }

    out[write_index] = '\0';
}

static void oclock_renderer_draw_temp_unit(u8g2_t *u8g2,
                                           int x,
                                           int baseline_y,
                                           uint8_t circle_radius,
                                           int circle_offset_y,
                                           int text_offset_x)
{
    u8g2_DrawCircle(u8g2,
                    x + (int)circle_radius,
                    baseline_y + circle_offset_y,
                    circle_radius,
                    U8G2_DRAW_ALL);
    u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
    u8g2_DrawStr(u8g2, x + text_offset_x, baseline_y, "C");
}

static void oclock_renderer_extract_percent_value(const char *source,
                                                  char *out,
                                                  size_t out_size)
{
    size_t write_index = 0;
    bool started = false;

    if (out_size == 0U) {
        return;
    }

    if (source != NULL) {
        for (size_t i = 0; source[i] != '\0' && write_index + 1U < out_size; ++i) {
            const char ch = source[i];
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '%') {
                out[write_index++] = ch;
                started = true;
            } else if (started) {
                break;
            }
        }
    }

    if (write_index == 0U) {
        snprintf(out, out_size, "--%%");
        return;
    }

    out[write_index] = '\0';
}

static void oclock_renderer_draw_weather_icon_sun(u8g2_t *u8g2,
                                                  int cx,
                                                  int cy,
                                                  uint8_t radius)
{
    const int outer = (int)radius + 6;
    const int inner = (int)radius + 2;

    u8g2_DrawCircle(u8g2, cx, cy, radius, U8G2_DRAW_ALL);
    u8g2_DrawLine(u8g2, cx, cy - outer, cx, cy - inner);
    u8g2_DrawLine(u8g2, cx, cy + inner, cx, cy + outer);
    u8g2_DrawLine(u8g2, cx - outer, cy, cx - inner, cy);
    u8g2_DrawLine(u8g2, cx + inner, cy, cx + outer, cy);
    u8g2_DrawLine(u8g2, cx - outer + 2, cy - outer + 2, cx - inner, cy - inner);
    u8g2_DrawLine(u8g2, cx + inner, cy - inner, cx + outer - 2, cy - outer + 2);
    u8g2_DrawLine(u8g2, cx - outer + 2, cy + outer - 2, cx - inner, cy + inner);
    u8g2_DrawLine(u8g2, cx + inner, cy + inner, cx + outer - 2, cy + outer - 2);
}

static void oclock_renderer_draw_weather_icon_cloud(u8g2_t *u8g2, int x, int y)
{
    u8g2_DrawCircle(u8g2, x + 14, y + 16, 8U, U8G2_DRAW_ALL);
    u8g2_DrawCircle(u8g2, x + 26, y + 12, 10U, U8G2_DRAW_ALL);
    u8g2_DrawCircle(u8g2, x + 39, y + 16, 8U, U8G2_DRAW_ALL);
    u8g2_DrawHLine(u8g2, x + 10, y + 24, 33);
    u8g2_DrawHLine(u8g2, x + 11, y + 25, 31);
    u8g2_DrawVLine(u8g2, x + 9, y + 17, 8);
    u8g2_DrawVLine(u8g2, x + 43, y + 17, 8);
}

static void oclock_renderer_draw_weather_icon_rain(u8g2_t *u8g2, int x, int y)
{
    oclock_renderer_draw_weather_icon_cloud(u8g2, x, y);
    u8g2_DrawLine(u8g2, x + 15, y + 29, x + 11, y + 35);
    u8g2_DrawLine(u8g2, x + 27, y + 29, x + 23, y + 35);
    u8g2_DrawLine(u8g2, x + 39, y + 29, x + 35, y + 35);
}

static void oclock_renderer_draw_weather_icon_snow(u8g2_t *u8g2, int x, int y)
{
    oclock_renderer_draw_weather_icon_cloud(u8g2, x, y);
    u8g2_DrawLine(u8g2, x + 18, y + 29, x + 18, y + 35);
    u8g2_DrawLine(u8g2, x + 15, y + 32, x + 21, y + 32);
    u8g2_DrawLine(u8g2, x + 31, y + 29, x + 31, y + 35);
    u8g2_DrawLine(u8g2, x + 28, y + 32, x + 34, y + 32);
}

static void oclock_renderer_draw_weather_icon_thunder(u8g2_t *u8g2, int x, int y)
{
    oclock_renderer_draw_weather_icon_cloud(u8g2, x, y);
    u8g2_DrawLine(u8g2, x + 27, y + 25, x + 21, y + 34);
    u8g2_DrawLine(u8g2, x + 21, y + 34, x + 28, y + 34);
    u8g2_DrawLine(u8g2, x + 28, y + 34, x + 23, y + 42);
}

static void oclock_renderer_draw_weather_icon(u8g2_t *u8g2,
                                              int x,
                                              int y,
                                              const char *summary_text)
{
    if (summary_text == NULL || summary_text[0] == '\0') {
        oclock_renderer_draw_weather_icon_sun(u8g2, x + 20, y + 18, 9U);
        return;
    }

    if (strstr(summary_text, "\xE9\x9B\xB7") != NULL) {
        oclock_renderer_draw_weather_icon_thunder(u8g2, x, y);
    } else if (strstr(summary_text, "\xE9\x9B\xAA") != NULL) {
        oclock_renderer_draw_weather_icon_snow(u8g2, x, y);
    } else if (strstr(summary_text, "\xE9\x9B\xA8") != NULL) {
        oclock_renderer_draw_weather_icon_rain(u8g2, x, y);
    } else if (strstr(summary_text, "\xE5\xA4\x9A\xE4\xBA\x91") != NULL ||
               strstr(summary_text, "\xE5\xB0\x91\xE4\xBA\x91") != NULL ||
               strstr(summary_text, "\xE6\x99\xB4") != NULL) {
        oclock_renderer_draw_weather_icon_sun(u8g2, x + 16, y + 12, 7U);
        oclock_renderer_draw_weather_icon_cloud(u8g2, x + 10, y + 8);
    } else if (strstr(summary_text, "\xE9\x98\xB4") != NULL ||
               strstr(summary_text, "\xE9\x9B\xBE") != NULL ||
               strstr(summary_text, "\xE4\xBA\x91") != NULL) {
        oclock_renderer_draw_weather_icon_cloud(u8g2, x, y);
    } else {
        oclock_renderer_draw_weather_icon_sun(u8g2, x + 20, y + 18, 9U);
    }
}

static void oclock_renderer_draw_home(u8g2_t *u8g2,
                                      const oclock_render_model_t *model)
{
    static const char *kBatteryLabel = "\xE7\x94\xB5\xE9\x87\x8F";
    static const char *kIndoorTempLabel = "\xE5\xAE\xA4\xE5\x86\x85\xE6\xB8\xA9\xE5\xBA\xA6";
    static const char *kIndoorHumidityLabel = "\xE5\xAE\xA4\xE5\x86\x85\xE6\xB9\xBF\xE5\xBA\xA6";
    static const char *kTodoLabel = "\xE4\xBB\x8A\xE6\x97\xA5\xE5\xBE\x85\xE5\x8A\x9E";
    static const char *kTodoEmpty = "\xE6\x9A\x82\xE6\x97\xA0\xE5\xBE\x85\xE5\x8A\x9E";
    static const char *kTodoRemainingFmt =
        "\xE5\x89\xA9\xE4\xBD\x99 %lu \xE9\xA1\xB9";
    char top_bar[80];
    char todo_summary[40];
    char second_text[4];
    char todo_line_1[OCLOCK_TODO_TITLE_MAX_LEN + 8];
    char todo_line_2[OCLOCK_TODO_TITLE_MAX_LEN + 8];
    char weather_temp_value[12];
    char env_temp_value[12];
    char env_humidity_value[12];
    const char *location_text = model->weather_view.location_text[0] != '\0' ?
        model->weather_view.location_text : "--";
    const char *weather_temp_text = model->weather_view.current_temp_text[0] != '\0' ?
        model->weather_view.current_temp_text : "--.-\xC2\xB0""C";
    const char *weather_summary_text = model->weather_view.summary_text[0] != '\0' ?
        model->weather_view.summary_text : "--";
    const char *env_temp_text = model->environment_view.temperature_text[0] != '\0' ?
        model->environment_view.temperature_text : "--.-\xC2\xB0""C";
    const char *env_humidity_text = model->environment_view.humidity_text[0] != '\0' ?
        model->environment_view.humidity_text : "--%";
    const int seconds = (int)((model->now_ms / 1000LL) % 60LL);
    const int top_right_padding = 22;
    const int reserved_battery_slot = 28;
    int battery_width = 0;
    int location_width = 0;
    int battery_x = 0;
    int location_x = 0;

    memset(todo_line_1, 0, sizeof(todo_line_1));
    memset(todo_line_2, 0, sizeof(todo_line_2));
    memset(weather_temp_value, 0, sizeof(weather_temp_value));
    memset(env_temp_value, 0, sizeof(env_temp_value));
    memset(env_humidity_value, 0, sizeof(env_humidity_value));
    snprintf(second_text, sizeof(second_text), "%02d", seconds);
    oclock_renderer_extract_temp_value(weather_temp_text,
                                       weather_temp_value,
                                       sizeof(weather_temp_value));
    oclock_renderer_extract_temp_value(env_temp_text,
                                       env_temp_value,
                                       sizeof(env_temp_value));
    oclock_renderer_extract_percent_value(env_humidity_text,
                                          env_humidity_value,
                                          sizeof(env_humidity_value));

    u8g2_DrawFrame(u8g2, 1, 1, OCLOCK_LCD_WIDTH - 2, OCLOCK_LCD_HEIGHT - 2);
    u8g2_DrawHLine(u8g2, 20, 42, 360);
    u8g2_DrawVLine(u8g2, 220, 54, 196);
    u8g2_DrawHLine(u8g2, 244, 174, 138);

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    snprintf(top_bar,
             sizeof(top_bar),
             "%s  %s",
             model->time_view.date_text,
             model->time_view.weekday_text);
    battery_width = (int)u8g2_GetUTF8Width(u8g2, kBatteryLabel);
    location_width = (int)u8g2_GetUTF8Width(u8g2, location_text);
    battery_x = OCLOCK_LCD_WIDTH - top_right_padding - reserved_battery_slot - battery_width;
    location_x = battery_x - 18 - location_width;
    if (location_x < 272) {
        location_x = 272;
    }
    u8g2_DrawUTF8(u8g2, 22, 28, top_bar);
    u8g2_DrawUTF8(u8g2, location_x, 28, location_text);
    u8g2_DrawUTF8(u8g2, battery_x, 28, kBatteryLabel);

    u8g2_SetFont(u8g2, u8g2_font_logisoso42_tn);
    u8g2_DrawStr(u8g2, 28, 152, model->time_view.time_text);

    u8g2_SetFont(u8g2, u8g2_font_logisoso18_tn);
    u8g2_DrawStr(u8g2, 146, 88, second_text);

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, 34, 214, kIndoorTempLabel);
    u8g2_SetFont(u8g2, u8g2_font_10x20_tf);
    u8g2_DrawStr(u8g2, 118, 214, env_temp_value);
    oclock_renderer_draw_temp_unit(u8g2,
                                   118 + (int)u8g2_GetStrWidth(u8g2, env_temp_value) + 4,
                                   214,
                                   2U,
                                   -14,
                                   8);
    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, 34, 252, kIndoorHumidityLabel);
    u8g2_SetFont(u8g2, u8g2_font_10x20_tf);
    u8g2_DrawStr(u8g2, 118, 252, env_humidity_value);

    u8g2_SetFont(u8g2, u8g2_font_logisoso24_tn);
    u8g2_DrawStr(u8g2, 248, 106, weather_temp_value);
    oclock_renderer_draw_temp_unit(u8g2,
                                   248 + (int)u8g2_GetStrWidth(u8g2, weather_temp_value) + 4,
                                   106,
                                   3U,
                                   -18,
                                   10);
    oclock_renderer_draw_weather_icon(u8g2, 334, 76, weather_summary_text);
    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, 248, 138, weather_summary_text);

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, 248, 194, kTodoLabel);
    snprintf(todo_summary,
             sizeof(todo_summary),
             kTodoRemainingFmt,
             (unsigned long)model->todo_view.open_items);
    u8g2_DrawUTF8(u8g2, 324, 194, todo_summary);

    if (model->todo_view.count > 0) {
        snprintf(todo_line_1, sizeof(todo_line_1), "%s", model->todo_view.entries[0].title);
    } else {
        snprintf(todo_line_1, sizeof(todo_line_1), "%s", kTodoEmpty);
    }
    if (model->todo_view.count > 1) {
        snprintf(todo_line_2, sizeof(todo_line_2), "%s", model->todo_view.entries[1].title);
    }

    u8g2_DrawFrame(u8g2, 248, 210, 18, 18);
    u8g2_DrawHLine(u8g2, 274, 226, 92);
    u8g2_DrawUTF8(u8g2, 274, 223, todo_line_1);

    u8g2_DrawFrame(u8g2, 248, 242, 18, 18);
    if (model->todo_view.count > 1) {
        u8g2_DrawLine(u8g2, 252, 251, 256, 256);
        u8g2_DrawLine(u8g2, 256, 256, 262, 247);
        u8g2_DrawUTF8(u8g2, 274, 255, todo_line_2);
    }
    u8g2_DrawHLine(u8g2, 274, 258, 92);
}

static void oclock_renderer_draw_top_bar(u8g2_t *u8g2,
                                         const oclock_render_model_t *model)
{
    static const char *kBatteryLabel = "\xE7\x94\xB5\xE9\x87\x8F";
    char top_bar[80];
    const char *location_text = model->weather_view.location_text[0] != '\0' ?
        model->weather_view.location_text : "--";
    const int top_right_padding = 22;
    const int reserved_battery_slot = 28;
    int battery_width = 0;
    int location_width = 0;
    int battery_x = 0;
    int location_x = 0;

    u8g2_DrawHLine(u8g2, 20, 42, 360);
    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    snprintf(top_bar,
             sizeof(top_bar),
             "%s  %s",
             model->time_view.date_text,
             model->time_view.weekday_text);
    battery_width = (int)u8g2_GetUTF8Width(u8g2, kBatteryLabel);
    location_width = (int)u8g2_GetUTF8Width(u8g2, location_text);
    battery_x = OCLOCK_LCD_WIDTH - top_right_padding - reserved_battery_slot - battery_width;
    location_x = battery_x - 18 - location_width;
    if (location_x < 272) {
        location_x = 272;
    }

    u8g2_DrawUTF8(u8g2, 22, 28, top_bar);
    u8g2_DrawUTF8(u8g2, location_x, 28, location_text);
    u8g2_DrawUTF8(u8g2, battery_x, 28, kBatteryLabel);
}

static void oclock_renderer_draw_weather(u8g2_t *u8g2,
                                         const oclock_render_model_t *model)
{
    static const char *kLabelYesterday = "\xE6\x98\xA8\xE5\xA4\xA9";
    static const char *kLabelToday = "\xE4\xBB\x8A\xE5\xA4\xA9";
    static const char *kLabelTomorrow = "\xE6\x98\x8E\xE5\xA4\xA9";
    static const char *kLabelAfterTomorrow = "\xE5\x90\x8E\xE5\xA4\xA9";
    static const char *kLabelCurrent = "\xE5\xBD\x93\xE5\x89\x8D";
    static const char *kLabelMax = "\xE6\x9C\x80\xE9\xAB\x98";
    static const char *kLabelMin = "\xE6\x9C\x80\xE4\xBD\x8E";
    char weather_temp_value[12];
    char humidity_value[12];
    char day_max_values[4][8];
    char day_min_values[4][8];
    char day_summaries[4][32];
    char day_dates[4][8];
    int high_points_x[4] = {54, 140, 232, 322};
    int high_points_y[4] = {232, 242, 238, 234};
    int low_points_x[4] = {54, 140, 232, 322};
    int low_points_y[4] = {274, 278, 276, 272};
    const char *current_summary = model->weather_view.summary_text[0] != '\0' ?
        model->weather_view.summary_text : "--";
    const char *current_humidity = model->weather_view.humidity_text[0] != '\0' ?
        model->weather_view.humidity_text : "--%";
    const int day_cols_x[4] = {34, 114, 204, 294};

    memset(weather_temp_value, 0, sizeof(weather_temp_value));
    memset(humidity_value, 0, sizeof(humidity_value));
    memset(day_max_values, 0, sizeof(day_max_values));
    memset(day_min_values, 0, sizeof(day_min_values));
    memset(day_summaries, 0, sizeof(day_summaries));
    memset(day_dates, 0, sizeof(day_dates));

    oclock_renderer_draw_top_bar(u8g2, model);

    oclock_renderer_extract_temp_value(model->weather_view.current_temp_text,
                                       weather_temp_value,
                                       sizeof(weather_temp_value));
    oclock_renderer_extract_temp_value(current_humidity,
                                       humidity_value,
                                       sizeof(humidity_value));

    snprintf(day_dates[0], sizeof(day_dates[0]), "--/--");
    snprintf(day_dates[1], sizeof(day_dates[1]), "%s", model->weather_view.today_date_text);
    snprintf(day_dates[2], sizeof(day_dates[2]), "%s", model->weather_view.day1_date_text);
    snprintf(day_dates[3], sizeof(day_dates[3]), "%s", model->weather_view.day2_date_text);

    snprintf(day_summaries[0], sizeof(day_summaries[0]), "%s", current_summary);
    snprintf(day_summaries[1], sizeof(day_summaries[1]), "%s", model->weather_view.today_summary_text);
    snprintf(day_summaries[2], sizeof(day_summaries[2]), "%s", model->weather_view.day1_summary_text);
    snprintf(day_summaries[3], sizeof(day_summaries[3]), "%s", model->weather_view.day2_summary_text);

    snprintf(day_max_values[0], sizeof(day_max_values[0]), "--");
    snprintf(day_max_values[1], sizeof(day_max_values[1]), "%s", model->weather_view.today_max_text);
    snprintf(day_max_values[2], sizeof(day_max_values[2]), "%s", model->weather_view.day1_max_text);
    snprintf(day_max_values[3], sizeof(day_max_values[3]), "%s", model->weather_view.day2_max_text);

    snprintf(day_min_values[0], sizeof(day_min_values[0]), "--");
    snprintf(day_min_values[1], sizeof(day_min_values[1]), "%s", model->weather_view.today_min_text);
    snprintf(day_min_values[2], sizeof(day_min_values[2]), "%s", model->weather_view.day1_min_text);
    snprintf(day_min_values[3], sizeof(day_min_values[3]), "%s", model->weather_view.day2_min_text);

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, 18, 74, kLabelCurrent);

    u8g2_SetFont(u8g2, u8g2_font_logisoso18_tn);
    u8g2_DrawStr(u8g2, 54, 102, weather_temp_value);
    oclock_renderer_draw_temp_unit(u8g2,
                                   54 + (int)u8g2_GetStrWidth(u8g2, weather_temp_value) + 2,
                                   102,
                                   2U,
                                   -14,
                                   8);

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    oclock_renderer_draw_weather_icon(u8g2, 14, 72, current_summary);
    u8g2_DrawUTF8(u8g2, 118, 102, current_summary);
    u8g2_DrawUTF8(u8g2, 302, 88, current_humidity);

    u8g2_DrawHLine(u8g2, 12, 120, 376);

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, day_cols_x[0], 142, kLabelYesterday);
    u8g2_DrawUTF8(u8g2, day_cols_x[1], 142, kLabelToday);
    u8g2_DrawUTF8(u8g2, day_cols_x[2], 142, kLabelTomorrow);
    u8g2_DrawUTF8(u8g2, day_cols_x[3], 142, kLabelAfterTomorrow);

    u8g2_SetFont(u8g2, u8g2_font_6x13_tf);
    for (int i = 0; i < 4; ++i) {
        u8g2_DrawStr(u8g2, day_cols_x[i] + 4, 160, day_dates[i]);
    }

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, day_cols_x[0] + 4, 178, current_summary);
    u8g2_DrawUTF8(u8g2, day_cols_x[1] + 4, 178, model->weather_view.today_summary_text);
    u8g2_DrawUTF8(u8g2, day_cols_x[2] + 4, 178, model->weather_view.day1_summary_text);
    u8g2_DrawUTF8(u8g2, day_cols_x[3] + 4, 178, model->weather_view.day2_summary_text);

    oclock_renderer_draw_weather_icon(u8g2, day_cols_x[0] + 6, 184, current_summary);
    oclock_renderer_draw_weather_icon(u8g2, day_cols_x[1] + 6, 184, model->weather_view.today_summary_text);
    oclock_renderer_draw_weather_icon(u8g2, day_cols_x[2] + 6, 184, model->weather_view.day1_summary_text);
    oclock_renderer_draw_weather_icon(u8g2, day_cols_x[3] + 6, 184, model->weather_view.day2_summary_text);

    u8g2_SetFont(u8g2, u8g2_font_6x13_tf);
    u8g2_DrawUTF8(u8g2, 14, 232, kLabelMax);
    u8g2_DrawUTF8(u8g2, 14, 274, kLabelMin);

    u8g2_DrawLine(u8g2, high_points_x[0], high_points_y[0],
                  high_points_x[1], high_points_y[1]);
    u8g2_DrawLine(u8g2, high_points_x[1], high_points_y[1],
                  high_points_x[2], high_points_y[2]);
    u8g2_DrawLine(u8g2, high_points_x[2], high_points_y[2],
                  high_points_x[3], high_points_y[3]);
    u8g2_DrawLine(u8g2, low_points_x[0], low_points_y[0],
                  low_points_x[1], low_points_y[1]);
    u8g2_DrawLine(u8g2, low_points_x[1], low_points_y[1],
                  low_points_x[2], low_points_y[2]);
    u8g2_DrawLine(u8g2, low_points_x[2], low_points_y[2],
                  low_points_x[3], low_points_y[3]);
    for (int i = 0; i < 4; ++i) {
        u8g2_DrawDisc(u8g2, high_points_x[i], high_points_y[i], 2U, U8G2_DRAW_ALL);
        u8g2_DrawDisc(u8g2, low_points_x[i], low_points_y[i], 2U, U8G2_DRAW_ALL);
    }

    for (int i = 0; i < 4; ++i) {
        u8g2_DrawStr(u8g2, high_points_x[i] - 8, high_points_y[i] - 6, day_max_values[i]);
        u8g2_DrawStr(u8g2, low_points_x[i] - 8, low_points_y[i] - 6, day_min_values[i]);
    }
}

static void oclock_renderer_draw_todo(u8g2_t *u8g2,
                                      const oclock_render_model_t *model)
{
    static const char *kOpenLabel = "\xE5\xBE\x85\xE5\x8A\x9E";
    static const char *kDoneLabel = "\xE5\xB7\xB2\xE5\xAE\x8C\xE6\x88\x90";
    static const char *kDueLabel = "\xE6\x88\xAA\xE6\xAD\xA2";
    static const char *kEmpty = "\xE6\x9A\x82\xE6\x97\xA0\xE5\xBE\x85\xE5\x8A\x9E";
    static const char *kHigh = "high";
    static const char *kMedium = "medium";
    char stat_open[24];
    char stat_done[24];
    char due_line[40];

    oclock_renderer_draw_top_bar(u8g2, model);

    u8g2_DrawVLine(u8g2, 200, 52, 56);
    u8g2_DrawHLine(u8g2, 18, 108, 364);

    u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
    u8g2_DrawUTF8(u8g2, 34, 78, kOpenLabel);
    u8g2_DrawUTF8(u8g2, 226, 78, kDoneLabel);
    u8g2_SetFont(u8g2, u8g2_font_logisoso18_tn);
    snprintf(stat_open, sizeof(stat_open), "%lu", (unsigned long)model->todo_view.open_items);
    snprintf(stat_done, sizeof(stat_done), "%lu", (unsigned long)model->todo_view.completed_items);
    u8g2_DrawStr(u8g2, 122, 96, stat_open);
    u8g2_DrawStr(u8g2, 314, 96, stat_done);

    if (model->todo_view.count == 0) {
        u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
        u8g2_DrawUTF8(u8g2, 132, 176, kEmpty);
        return;
    }

    int card_y = 122;
    for (size_t i = 0; i < model->todo_view.count && i < 3; ++i) {
        const oclock_todo_display_entry_t *entry = &model->todo_view.entries[i];
        int title_y = card_y + 18;
        int meta_y = card_y + 33;

        if (i > 0) {
            u8g2_DrawHLine(u8g2, 18, card_y - 6, 364);
        }
        u8g2_DrawFrame(u8g2, 28, card_y + 10, 14, 14);

        if (strcmp(entry->priority_text, kHigh) == 0) {
            u8g2_DrawBox(u8g2, 350, card_y + 12, 12, 12);
        } else if (strcmp(entry->priority_text, kMedium) == 0) {
            u8g2_DrawFrame(u8g2, 350, card_y + 12, 12, 12);
            u8g2_DrawLine(u8g2, 352, card_y + 18, 360, card_y + 18);
        } else {
            u8g2_DrawFrame(u8g2, 350, card_y + 12, 12, 12);
        }

        u8g2_SetFont(u8g2, u8g2_font_wqy14_t_gb2312b);
        u8g2_DrawUTF8(u8g2, 52, title_y, entry->title);

        u8g2_SetFont(u8g2, u8g2_font_6x13_tf);
        snprintf(due_line, sizeof(due_line), "%s %s", kDueLabel, entry->due_text);
        u8g2_DrawUTF8(u8g2, 52, meta_y, due_line);
        if (entry->overdue) {
            u8g2_DrawStr(u8g2, 262, meta_y, "OVERDUE");
        }

        card_y += 46;
    }
}

static void oclock_renderer_draw_album(u8g2_t *u8g2,
                                       const oclock_render_model_t *model)
{
    if (!model->album_view.valid || model->album_view.frame_data == NULL) {
        u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
        u8g2_DrawStr(u8g2, 120, 148, "Album empty");
        u8g2_DrawStr(u8g2, 82, 170, "Upload image frames in web");
    } else {
        const uint8_t *frame = model->album_view.frame_data;
        const uint16_t width = model->album_view.width;
        const uint16_t height = model->album_view.height;
        const int start_x = 0;
        const int start_y = 0;
        for (uint16_t y = 0; y < height; ++y) {
            for (uint16_t x = 0; x < width; ++x) {
                size_t bit_index = (size_t)y * width + x;
                if ((frame[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 0x01U) {
                    u8g2_DrawPixel(u8g2, start_x + x, start_y + y);
                }
            }
        }
    }
}

static bool oclock_renderer_read_logical_pixel(const u8g2_t *u8g2,
                                               uint16_t logical_x,
                                               uint16_t logical_y)
{
    const uint16_t physical_x = (uint16_t)((OCLOCK_LCD_HEIGHT - 1U) - logical_y);
    const uint16_t physical_y = logical_x;
    const uint16_t buffer_width = (uint16_t)(u8g2_GetBufferTileWidth(u8g2) * 8U);
    const uint32_t index =
        (uint32_t)physical_x + ((uint32_t)(physical_y >> 3U) * buffer_width);
    const uint8_t mask = (uint8_t)(1U << (physical_y & 0x07U));
    const uint8_t *buffer = u8g2_GetBufferPtr(u8g2);

    return (buffer[index] & mask) != 0;
}

static esp_err_t oclock_renderer_present_canvas(void)
{
    u8g2_t *u8g2 = &g_canvas.u8g2;

    oclock_rlcd_port_clear(false);
    for (uint16_t y = 0; y < OCLOCK_LCD_HEIGHT; ++y) {
        for (uint16_t x = 0; x < OCLOCK_LCD_WIDTH; ++x) {
            if (oclock_renderer_read_logical_pixel(u8g2, x, y)) {
                oclock_rlcd_port_set_pixel(x, y, true);
            }
        }
    }
    return oclock_rlcd_port_present();
}

static void oclock_renderer_draw_page(const oclock_render_model_t *model)
{
    u8g2_t *u8g2 = &g_canvas.u8g2;

    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);

    if (model->scheduler.current_page == OCLOCK_PAGE_HOME) {
        oclock_renderer_draw_home(u8g2, model);
    } else if (model->scheduler.current_page == OCLOCK_PAGE_WEATHER) {
        oclock_renderer_draw_weather(u8g2, model);
    } else if (model->scheduler.current_page == OCLOCK_PAGE_TODO) {
        oclock_renderer_draw_todo(u8g2, model);
    } else if (model->scheduler.current_page == OCLOCK_PAGE_ALBUM) {
        oclock_renderer_draw_album(u8g2, model);
    } else {
        oclock_renderer_draw_page_frame(u8g2, model);
        oclock_renderer_draw_album(u8g2, model);
    }

    if (model->audio_status.state == OCLOCK_AUDIO_RECORDING ||
        model->audio_status.state == OCLOCK_AUDIO_PLAYING ||
        model->audio_status.state == OCLOCK_AUDIO_PREPARING) {
        char status_text[32];
        if (model->audio_status.state == OCLOCK_AUDIO_RECORDING) {
            snprintf(status_text, sizeof(status_text), "REC %lu/5 s  BOOT:STOP",
                     (unsigned long)(model->audio_status.elapsed_ms / 1000U));
        } else if (model->audio_status.state == OCLOCK_AUDIO_PLAYING) {
            snprintf(status_text, sizeof(status_text), "PLAYING  BOOT:STOP");
        } else {
            snprintf(status_text, sizeof(status_text), "AUDIO STARTING");
        }
        u8g2_SetFont(u8g2, u8g2_font_6x13_tf);
        const int width = (int)u8g2_GetStrWidth(u8g2, status_text) + 16;
        const int x = (OCLOCK_LCD_WIDTH - width) / 2;
        u8g2_SetDrawColor(u8g2, 1);
        u8g2_DrawBox(u8g2, x, OCLOCK_LCD_HEIGHT - 25, width, 18);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, x + 8, OCLOCK_LCD_HEIGHT - 12, status_text);
        u8g2_SetDrawColor(u8g2, 1);
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(oclock_renderer_present_canvas());
}

esp_err_t oclock_renderer_init(void)
{
    g_canvas.buffer = (uint8_t *)heap_caps_malloc(38U * 50U * 8U,
                                                  MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (g_canvas.buffer == NULL) {
        g_canvas.buffer = (uint8_t *)heap_caps_malloc(38U * 50U * 8U,
                                                      MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(g_canvas.buffer != NULL,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "canvas allocation failed");

    memset(g_canvas.buffer, 0, 38U * 50U * 8U);

    u8g2_SetupDisplay(&g_canvas.u8g2,
                      oclock_virtual_display_cb,
                      u8x8_cad_empty,
                      oclock_virtual_byte_cb,
                      oclock_virtual_gpio_and_delay_cb);
    u8g2_SetupBuffer(&g_canvas.u8g2,
                     g_canvas.buffer,
                     50,
                     u8g2_ll_hvline_vertical_top_lsb,
                     U8G2_R1);

    ESP_RETURN_ON_ERROR(oclock_rlcd_port_init(), TAG, "rlcd init failed");
    g_canvas.ready = true;
    ESP_LOGI(TAG, "Renderer initialized with virtual u8g2 canvas + official RLCD path");
    return ESP_OK;
}

void oclock_renderer_render(const oclock_render_model_t *model)
{
    if (g_canvas.ready) {
        oclock_renderer_draw_page(model);
    }
    oclock_renderer_log_page(model);
}
