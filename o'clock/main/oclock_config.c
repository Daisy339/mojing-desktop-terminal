#include "oclock_config.h"

#include <string.h>

#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "oclock_config";
static const char *kNamespace = "oclock_cfg";
static const char *kWifiSsidKey = "wifi_ssid";
static const char *kWifiPassKey = "wifi_pass";
static const char *kWeatherCityKey = "weather_city";
static const char *kWeatherLatKey = "weather_lat";
static const char *kWeatherLonKey = "weather_lon";

esp_err_t oclock_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t oclock_config_load(oclock_device_config_t *out_config)
{
    ESP_RETURN_ON_FALSE(out_config != NULL, ESP_ERR_INVALID_ARG, TAG, "out_config is null");

    memset(out_config, 0, sizeof(*out_config));

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        out_config->wifi_configured = false;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs_open failed");

    size_t ssid_len = sizeof(out_config->wifi_ssid);
    ret = nvs_get_str(handle, kWifiSsidKey, out_config->wifi_ssid, &ssid_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        out_config->wifi_configured = false;
        nvs_close(handle);
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    size_t pass_len = sizeof(out_config->wifi_password);
    ret = nvs_get_str(handle, kWifiPassKey, out_config->wifi_password, &pass_len);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ret;
    }

    out_config->wifi_configured = out_config->wifi_ssid[0] != '\0';

    size_t city_len = sizeof(out_config->weather_city);
    ret = nvs_get_str(handle, kWeatherCityKey, out_config->weather_city, &city_len);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_get_blob(handle,
                       kWeatherLatKey,
                       &out_config->weather_latitude,
                       &(size_t){sizeof(out_config->weather_latitude)});
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_get_blob(handle,
                       kWeatherLonKey,
                       &out_config->weather_longitude,
                       &(size_t){sizeof(out_config->weather_longitude)});
    nvs_close(handle);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    }

    out_config->weather_configured =
        out_config->weather_city[0] != '\0';
    return ESP_OK;
}

esp_err_t oclock_config_save_wifi(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid != NULL, ESP_ERR_INVALID_ARG, TAG, "ssid is null");
    ESP_RETURN_ON_FALSE(password != NULL, ESP_ERR_INVALID_ARG, TAG, "password is null");

    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, kWifiSsidKey, ssid), TAG, "nvs_set_str ssid failed");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, kWifiPassKey, password), TAG, "nvs_set_str pass failed");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "nvs_commit failed");
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t oclock_config_save_weather(const char *city_name,
                                     double latitude,
                                     double longitude)
{
    ESP_RETURN_ON_FALSE(city_name != NULL, ESP_ERR_INVALID_ARG, TAG, "city_name is null");

    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, kWeatherCityKey, city_name), TAG, "save weather city failed");
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle,
                                     kWeatherLatKey,
                                     &latitude,
                                     sizeof(latitude)),
                        TAG,
                        "save weather latitude failed");
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle,
                                     kWeatherLonKey,
                                     &longitude,
                                     sizeof(longitude)),
                        TAG,
                        "save weather longitude failed");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "nvs_commit failed");
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t oclock_config_clear_wifi(void)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs_open failed");
    ESP_RETURN_ON_ERROR(nvs_erase_key(handle, kWifiSsidKey), TAG, "erase ssid failed");
    ret = nvs_erase_key(handle, kWifiPassKey);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ret;
    }
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "nvs_commit failed");
    nvs_close(handle);
    return ESP_OK;
}
