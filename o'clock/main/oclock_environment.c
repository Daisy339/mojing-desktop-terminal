#include "oclock_environment.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "oclock_i2c_bus.h"

static const char *TAG = "oclock_env";
static const uint8_t kShtc3Address = 0x70;
static const int64_t kSampleIntervalMs = 10000;

typedef enum {
    NO_ERROR = 0x00,
    ACK_ERROR = 0x01,
    CHECKSUM_ERROR = 0x02,
} et_error_t;

typedef enum {
    READ_ID = 0xEFC8,
    SOFT_RESET = 0x805D,
    SLEEP = 0xB098,
    WAKEUP = 0x3517,
    MEAS_T_RH_POLLING = 0x7866,
} et_command_t;

typedef struct {
    i2c_master_dev_handle_t dev_handle;
    uint16_t sensor_id;
    float temperature_c;
    float humidity_percent;
    int64_t last_sample_ms;
    uint32_t version;
    bool initialized;
    bool valid;
} oclock_environment_context_t;

static oclock_environment_context_t g_env = {0};

static et_error_t shtc3_check_crc(const uint8_t *data, uint8_t bytes, uint8_t checksum)
{
    const uint16_t polynomial = 0x131;
    uint8_t crc = 0xFF;
    for (uint8_t byte_ctr = 0; byte_ctr < bytes; ++byte_ctr) {
        crc ^= data[byte_ctr];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ polynomial);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc == checksum ? NO_ERROR : CHECKSUM_ERROR;
}

static float shtc3_calc_temperature(uint16_t raw_value)
{
    const uint8_t petp_vol = 4;
    return 175.0f * (float)raw_value / 65536.0f - 45.0f - petp_vol;
}

static float shtc3_calc_humidity(uint16_t raw_value)
{
    return 100.0f * (float)raw_value / 65536.0f;
}

static esp_err_t shtc3_send_command(et_command_t command)
{
    uint8_t buf[2] = {(uint8_t)(command >> 8), (uint8_t)(command & 0xFF)};
    return oclock_i2c_write_register(g_env.dev_handle, -1, buf, 2);
}

static esp_err_t shtc3_wakeup(void)
{
    ESP_RETURN_ON_ERROR(shtc3_send_command(WAKEUP), TAG, "wakeup failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t shtc3_sleep(void)
{
    return shtc3_send_command(SLEEP);
}

static esp_err_t shtc3_soft_reset(void)
{
    return shtc3_send_command(SOFT_RESET);
}

static esp_err_t shtc3_get_id(void)
{
    uint8_t send_buf[2] = {(uint8_t)(READ_ID >> 8), (uint8_t)(READ_ID & 0xFF)};
    uint8_t read_buf[3] = {0};
    ESP_RETURN_ON_ERROR(oclock_i2c_write_register(g_env.dev_handle, -1, send_buf, 2), TAG, "id cmd failed");
    ESP_RETURN_ON_ERROR(oclock_i2c_read_register(g_env.dev_handle, -1, read_buf, 3), TAG, "id read failed");
    ESP_RETURN_ON_FALSE(shtc3_check_crc(read_buf, 2, read_buf[2]) == NO_ERROR,
                        ESP_ERR_INVALID_CRC,
                        TAG,
                        "id crc failed");
    g_env.sensor_id = (uint16_t)((read_buf[0] << 8) | read_buf[1]);
    return ESP_OK;
}

static esp_err_t shtc3_sample(float *temperature_c, float *humidity_percent)
{
    uint8_t send_buf[2] = {(uint8_t)(MEAS_T_RH_POLLING >> 8), (uint8_t)(MEAS_T_RH_POLLING & 0xFF)};
    uint8_t read_buf[6] = {0};
    ESP_RETURN_ON_ERROR(oclock_i2c_write_register(g_env.dev_handle, -1, send_buf, 2), TAG, "measure cmd failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(oclock_i2c_read_register(g_env.dev_handle, -1, read_buf, 6), TAG, "measure read failed");
    ESP_RETURN_ON_FALSE(shtc3_check_crc(read_buf, 2, read_buf[2]) == NO_ERROR,
                        ESP_ERR_INVALID_CRC,
                        TAG,
                        "temp crc failed");
    ESP_RETURN_ON_FALSE(shtc3_check_crc(&read_buf[3], 2, read_buf[5]) == NO_ERROR,
                        ESP_ERR_INVALID_CRC,
                        TAG,
                        "humidity crc failed");

    uint16_t raw_temp = (uint16_t)((read_buf[0] << 8) | read_buf[1]);
    uint16_t raw_humi = (uint16_t)((read_buf[3] << 8) | read_buf[4]);
    *temperature_c = shtc3_calc_temperature(raw_temp);
    *humidity_percent = shtc3_calc_humidity(raw_humi);
    return ESP_OK;
}

static const char *comfort_text(float temperature_c, float humidity_percent)
{
    if (temperature_c >= 20.0f && temperature_c <= 28.0f &&
        humidity_percent >= 40.0f && humidity_percent <= 70.0f) {
        return "舒适";
    }
    if (temperature_c > 28.0f) {
        return "偏热";
    }
    if (temperature_c < 18.0f) {
        return "偏凉";
    }
    if (humidity_percent > 75.0f) {
        return "偏潮";
    }
    if (humidity_percent < 35.0f) {
        return "偏干";
    }
    return "正常";
}

esp_err_t oclock_environment_init(void)
{
    if (g_env.initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(oclock_i2c_bus_add_device(kShtc3Address, 400000, &g_env.dev_handle),
                        TAG,
                        "shtc3 add device failed");
    ESP_RETURN_ON_ERROR(shtc3_wakeup(), TAG, "shtc3 wakeup failed");
    ESP_RETURN_ON_ERROR(shtc3_soft_reset(), TAG, "shtc3 reset failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(shtc3_get_id(), TAG, "shtc3 get id failed");
    ESP_LOGI(TAG, "SHTC3 ready: id=%04x", g_env.sensor_id);
    g_env.initialized = true;
    return ESP_OK;
}

esp_err_t oclock_environment_tick(void)
{
    if (!g_env.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    if (g_env.valid && (now_ms - g_env.last_sample_ms) < kSampleIntervalMs) {
        return ESP_OK;
    }

    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;
    if (shtc3_wakeup() == ESP_OK &&
        shtc3_sample(&temperature_c, &humidity_percent) == ESP_OK) {
        g_env.temperature_c = temperature_c;
        g_env.humidity_percent = humidity_percent;
        g_env.last_sample_ms = now_ms;
        g_env.valid = true;
        g_env.version++;
        ESP_LOGI(TAG, "Sampled env: %.1f C, %.1f%%", temperature_c, humidity_percent);
    }
    (void)shtc3_sleep();
    return ESP_OK;
}

void oclock_environment_build_view(const oclock_datetime_t *current_time,
                                   oclock_environment_view_t *out_view)
{
    memset(out_view, 0, sizeof(*out_view));
    if (!g_env.valid) {
        snprintf(out_view->temperature_text, sizeof(out_view->temperature_text), "--.-°C");
        snprintf(out_view->humidity_text, sizeof(out_view->humidity_text), "室内湿度 --%%");
        snprintf(out_view->sampled_text, sizeof(out_view->sampled_text), "等待首次采样");
        snprintf(out_view->comfort_text, sizeof(out_view->comfort_text), "暂无传感器数据");
        out_view->valid = false;
        return;
    }

    snprintf(out_view->temperature_text,
             sizeof(out_view->temperature_text),
             "%.1f°C",
             g_env.temperature_c);
    snprintf(out_view->humidity_text,
             sizeof(out_view->humidity_text),
             "室内湿度 %.0f%%",
             g_env.humidity_percent);
    if (current_time != NULL && current_time->valid) {
        snprintf(out_view->sampled_text,
                 sizeof(out_view->sampled_text),
                 "采样时间 %02d:%02d",
                 current_time->hour,
                 current_time->minute);
    } else {
        snprintf(out_view->sampled_text, sizeof(out_view->sampled_text), "采样已缓存");
    }
    snprintf(out_view->comfort_text,
             sizeof(out_view->comfort_text),
             "%s",
             comfort_text(g_env.temperature_c, g_env.humidity_percent));
    out_view->valid = true;
}

uint32_t oclock_environment_get_version(void)
{
    return g_env.version;
}
