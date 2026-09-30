#ifndef OCLOCK_CONFIG_H
#define OCLOCK_CONFIG_H

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char weather_city[48];
    double weather_latitude;
    double weather_longitude;
    bool wifi_configured;
    bool weather_configured;
} oclock_device_config_t;

esp_err_t oclock_config_init(void);
esp_err_t oclock_config_load(oclock_device_config_t *out_config);
esp_err_t oclock_config_save_wifi(const char *ssid, const char *password);
esp_err_t oclock_config_clear_wifi(void);
esp_err_t oclock_config_save_weather(const char *city_name,
                                     double latitude,
                                     double longitude);

#endif
