#ifndef OCLOCK_WEATHER_H
#define OCLOCK_WEATHER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "oclock_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char city_name[48];
    double latitude;
    double longitude;
    bool configured;
} oclock_weather_location_t;

typedef struct {
    char summary_text[24];
    char location_text[48];
    char current_temp_text[16];
    char humidity_text[20];
    char today_range_text[24];
    char today_date_text[8];
    char day1_date_text[8];
    char day2_date_text[8];
    char today_summary_text[24];
    char day1_summary_text[24];
    char day2_summary_text[24];
    char today_max_text[8];
    char today_min_text[8];
    char day1_max_text[8];
    char day1_min_text[8];
    char day2_max_text[8];
    char day2_min_text[8];
    char day1_text[32];
    char day2_text[32];
    char updated_text[48];
    bool valid;
    bool stale;
} oclock_weather_view_t;

esp_err_t oclock_weather_init(void);
esp_err_t oclock_weather_tick(void);
esp_err_t oclock_weather_request_refresh(void);
esp_err_t oclock_weather_set_location(const char *city_name);
esp_err_t oclock_weather_get_location(oclock_weather_location_t *out_location);
void oclock_weather_build_view(const oclock_datetime_t *current_time,
                               oclock_weather_view_t *out_view);
uint32_t oclock_weather_get_version(void);

#ifdef __cplusplus
}
#endif

#endif
