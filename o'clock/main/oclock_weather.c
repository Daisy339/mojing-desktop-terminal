#include "oclock_weather.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "oclock_config.h"
#include "oclock_network.h"

static const char *TAG = "oclock_weather";
static const int64_t kWeatherRefreshMs = 30LL * 60LL * 1000LL;
static const int64_t kRefreshRetryMs = 5LL * 60LL * 1000LL;
static const int64_t kLocationRetryMs = 60LL * 1000LL;
static const size_t kHttpBufferCapacity = 65536;

typedef struct {
    int weather_code;
    float temperature_c;
    float humidity_percent;
    float temp_max_c;
    float temp_min_c;
    int daily_codes[2];
    float daily_max_c[2];
    float daily_min_c[2];
    char current_summary[24];
    char daily_summary[2][24];
    char timezone[40];
    char updated_iso[32];
    bool valid;
} oclock_weather_cache_t;

typedef struct {
    oclock_weather_location_t location;
    oclock_weather_cache_t cache;
    uint32_t version;
    bool initialized;
    bool refresh_requested;
    int64_t last_attempt_ms;
    int64_t last_success_ms;
} oclock_weather_context_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} http_buffer_t;

static oclock_weather_context_t g_weather = {0};

static const char *weather_code_summary(int code)
{
    switch (code) {
    case 113:
        return "\xE6\x99\xB4";
    case 116:
        return "\xE5\xA4\x9A\xE4\xBA\x91";
    case 119:
    case 122:
        return "\xE9\x98\xB4";
    case 143:
    case 248:
    case 260:
        return "\xE9\x9B\xBE";
    case 176:
    case 263:
    case 266:
    case 281:
    case 284:
    case 293:
    case 296:
    case 299:
    case 302:
    case 305:
    case 308:
    case 311:
    case 314:
    case 353:
    case 356:
    case 359:
        return "\xE4\xB8\x8B\xE9\x9B\xA8";
    case 179:
    case 182:
    case 185:
    case 227:
    case 230:
    case 317:
    case 320:
    case 323:
    case 326:
    case 329:
    case 332:
    case 335:
    case 338:
    case 362:
    case 365:
    case 368:
    case 371:
    case 374:
    case 377:
    case 392:
    case 395:
        return "\xE4\xB8\x8B\xE9\x9B\xAA";
    case 200:
    case 386:
    case 389:
        return "\xE9\x9B\xB7\xE9\x9B\xA8";
    default:
        return "\xE6\x9C\xAA\xE7\x9F\xA5";
    }
}

static const char *weather_desc_summary(const char *desc)
{
    char lower[64];
    size_t i = 0;

    if (desc == NULL) {
        return NULL;
    }

    for (; desc[i] != '\0' && i + 1 < sizeof(lower); ++i) {
        lower[i] = (char)tolower((unsigned char)desc[i]);
    }
    lower[i] = '\0';

    if (strstr(lower, "thunder") != NULL) {
        return "\xE9\x9B\xB7\xE9\x9B\xA8";
    }
    if (strstr(lower, "snow") != NULL ||
        strstr(lower, "blizzard") != NULL ||
        strstr(lower, "sleet") != NULL ||
        strstr(lower, "ice") != NULL) {
        return "\xE4\xB8\x8B\xE9\x9B\xAA";
    }
    if (strstr(lower, "rain") != NULL ||
        strstr(lower, "drizzle") != NULL ||
        strstr(lower, "shower") != NULL) {
        return "\xE4\xB8\x8B\xE9\x9B\xA8";
    }
    if (strstr(lower, "mist") != NULL ||
        strstr(lower, "fog") != NULL ||
        strstr(lower, "haze") != NULL ||
        strstr(lower, "smoke") != NULL) {
        return "\xE9\x98\xB4";
    }
    if (strstr(lower, "partly cloudy") != NULL) {
        return "\xE5\xA4\x9A\xE4\xBA\x91";
    }
    if (strstr(lower, "cloudy") != NULL ||
        strstr(lower, "overcast") != NULL) {
        return "\xE9\x98\xB4";
    }
    if (strstr(lower, "sunny") != NULL ||
        strstr(lower, "clear") != NULL) {
        return "\xE6\x99\xB4";
    }

    return NULL;
}

static const char *weather_desc_value(cJSON *desc_array)
{
    cJSON *item = cJSON_IsArray(desc_array) ? cJSON_GetArrayItem(desc_array, 0) : NULL;
    cJSON *value = item ? cJSON_GetObjectItemCaseSensitive(item, "value") : NULL;
    return cJSON_IsString(value) && value->valuestring != NULL ? value->valuestring : NULL;
}

static void copy_summary_from_desc_or_code(cJSON *desc_array,
                                           int code,
                                           char *out,
                                           size_t out_size)
{
    const char *desc = weather_desc_value(desc_array);
    const char *summary = weather_desc_summary(desc);
    if (summary == NULL) {
        summary = weather_code_summary(code);
    }
    snprintf(out, out_size, "%s", summary);
}

static int json_int_value(cJSON *item, int default_value)
{
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return atoi(item->valuestring);
    }
    return default_value;
}

static float json_float_value(cJSON *item, float default_value)
{
    if (cJSON_IsNumber(item)) {
        return (float)item->valuedouble;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return (float)atof(item->valuestring);
    }
    return default_value;
}

static bool is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static void add_days(int *year, int *month, int *day, int delta_days)
{
    while (delta_days > 0) {
        int dim = days_in_month(*year, *month);
        (*day)++;
        if (*day > dim) {
            *day = 1;
            (*month)++;
            if (*month > 12) {
                *month = 1;
                (*year)++;
            }
        }
        delta_days--;
    }
}

static void format_offset_date(const oclock_datetime_t *current_time,
                               int offset_days,
                               char *out,
                               size_t out_size)
{
    if (current_time == NULL || !current_time->valid) {
        snprintf(out, out_size, "--/--");
        return;
    }

    int year = current_time->year;
    int month = current_time->month;
    int day = current_time->day;
    add_days(&year, &month, &day, offset_days);
    snprintf(out, out_size, "%02d/%02d", month, day);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buffer_t *buffer = (http_buffer_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && buffer != NULL && evt->data != NULL) {
        size_t copy_len = evt->data_len;
        if (buffer->buffer == NULL || buffer->capacity == 0) {
            return ESP_FAIL;
        }
        if (buffer->length + copy_len >= buffer->capacity) {
            copy_len = buffer->capacity - buffer->length - 1;
        }
        if (copy_len > 0) {
            memcpy(buffer->buffer + buffer->length, evt->data, copy_len);
            buffer->length += copy_len;
            buffer->buffer[buffer->length] = '\0';
        }
    }
    return ESP_OK;
}

static esp_err_t http_buffer_init(http_buffer_t *buffer)
{
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "buffer is null");
    memset(buffer, 0, sizeof(*buffer));
    buffer->buffer = (char *)malloc(kHttpBufferCapacity);
    ESP_RETURN_ON_FALSE(buffer->buffer != NULL,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "weather http buffer alloc failed");
    buffer->capacity = kHttpBufferCapacity;
    buffer->buffer[0] = '\0';
    return ESP_OK;
}

static void http_buffer_free(http_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }
    free(buffer->buffer);
    buffer->buffer = NULL;
    buffer->capacity = 0;
    buffer->length = 0;
}

static void url_encode_city(const char *src, char *dst, size_t dst_len)
{
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 4 < dst_len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = (char)c;
        } else if (c == ' ') {
            dst[di++] = '%';
            dst[di++] = '2';
            dst[di++] = '0';
        } else {
            snprintf(dst + di, dst_len - di, "%%%02X", c);
            di += 3;
        }
    }
    dst[di] = '\0';
}

static esp_err_t http_get_json(const char *url, http_buffer_t *buffer)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = buffer,
        .user_agent = "OClockTerminal/1.0",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,
        .timeout_ms = 10000,
    };

    ESP_RETURN_ON_FALSE(buffer != NULL && buffer->buffer != NULL && buffer->capacity > 0,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "http buffer not initialized");
    buffer->length = 0;
    buffer->buffer[0] = '\0';

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "http client init failed");
    esp_http_client_set_header(client, "Connection", "close");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            err = ESP_FAIL;
            ESP_LOGW(TAG, "HTTP GET failed with status %d for %s", status, url);
        }
    }
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t parse_geocoding(http_buffer_t *buffer,
                                 double *latitude,
                                 double *longitude)
{
    cJSON *root = cJSON_Parse(buffer->buffer);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_FAIL, TAG, "geocoding json parse failed");

    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    cJSON *first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : NULL;
    cJSON *lat = first ? cJSON_GetObjectItemCaseSensitive(first, "latitude") : NULL;
    cJSON *lon = first ? cJSON_GetObjectItemCaseSensitive(first, "longitude") : NULL;

    esp_err_t ret = ESP_FAIL;
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        *latitude = lat->valuedouble;
        *longitude = lon->valuedouble;
        ret = ESP_OK;
    }

    cJSON_Delete(root);
    return ret;
}

static esp_err_t parse_forecast(http_buffer_t *buffer)
{
    cJSON *root = cJSON_Parse(buffer->buffer);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_FAIL, TAG, "forecast json parse failed");

    cJSON *current_array = cJSON_GetObjectItemCaseSensitive(root, "current_condition");
    cJSON *weather_array = cJSON_GetObjectItemCaseSensitive(root, "weather");
    cJSON *current = cJSON_IsArray(current_array) ? cJSON_GetArrayItem(current_array, 0) : NULL;

    esp_err_t ret = ESP_FAIL;
    if (current != NULL && cJSON_IsArray(weather_array) && cJSON_GetArraySize(weather_array) >= 3) {
        cJSON *today = cJSON_GetArrayItem(weather_array, 0);
        cJSON *current_desc = current ? cJSON_GetObjectItemCaseSensitive(current, "weatherDesc") : NULL;
        g_weather.cache.temperature_c =
            json_float_value(cJSON_GetObjectItemCaseSensitive(current, "temp_C"), 0.0f);
        g_weather.cache.humidity_percent =
            json_float_value(cJSON_GetObjectItemCaseSensitive(current, "humidity"), 0.0f);
        g_weather.cache.weather_code =
            json_int_value(cJSON_GetObjectItemCaseSensitive(current, "weatherCode"), -1);
        copy_summary_from_desc_or_code(current_desc,
                                       g_weather.cache.weather_code,
                                       g_weather.cache.current_summary,
                                       sizeof(g_weather.cache.current_summary));
        g_weather.cache.temp_max_c =
            json_float_value(cJSON_GetObjectItemCaseSensitive(today, "maxtempC"), 0.0f);
        g_weather.cache.temp_min_c =
            json_float_value(cJSON_GetObjectItemCaseSensitive(today, "mintempC"), 0.0f);

        for (int i = 0; i < 2; ++i) {
            cJSON *day = cJSON_GetArrayItem(weather_array, i + 1);
            cJSON *hourly = day ? cJSON_GetObjectItemCaseSensitive(day, "hourly") : NULL;
            cJSON *hour = cJSON_IsArray(hourly) ?
                cJSON_GetArrayItem(hourly, cJSON_GetArraySize(hourly) > 4 ? 4 : 0) : NULL;
            cJSON *hour_desc = hour ? cJSON_GetObjectItemCaseSensitive(hour, "weatherDesc") : NULL;
            g_weather.cache.daily_codes[i] =
                json_int_value(hour ? cJSON_GetObjectItemCaseSensitive(hour, "weatherCode") : NULL,
                               -1);
            copy_summary_from_desc_or_code(hour_desc,
                                           g_weather.cache.daily_codes[i],
                                           g_weather.cache.daily_summary[i],
                                           sizeof(g_weather.cache.daily_summary[i]));
            g_weather.cache.daily_max_c[i] =
                json_float_value(day ? cJSON_GetObjectItemCaseSensitive(day, "maxtempC") : NULL,
                                 0.0f);
            g_weather.cache.daily_min_c[i] =
                json_float_value(day ? cJSON_GetObjectItemCaseSensitive(day, "mintempC") : NULL,
                                 0.0f);
        }

        cJSON *observation_time = cJSON_GetObjectItemCaseSensitive(current, "observation_time");
        if (cJSON_IsString(observation_time) && observation_time->valuestring != NULL) {
            strncpy(g_weather.cache.updated_iso,
                    observation_time->valuestring,
                    sizeof(g_weather.cache.updated_iso) - 1);
        }
        strncpy(g_weather.cache.timezone, "local", sizeof(g_weather.cache.timezone) - 1);
        g_weather.cache.valid = true;
        ret = ESP_OK;
    }

    cJSON_Delete(root);
    return ret;
}

static esp_err_t resolve_location_if_needed(void)
{
    if (!g_weather.location.configured ||
        g_weather.location.latitude != 0.0 ||
        g_weather.location.longitude != 0.0) {
        return ESP_OK;
    }

    char encoded_city[160];
    char url[320];
    http_buffer_t buffer = {0};
    ESP_RETURN_ON_ERROR(http_buffer_init(&buffer), TAG, "geocoding buffer init failed");
    url_encode_city(g_weather.location.city_name, encoded_city, sizeof(encoded_city));
    snprintf(url,
             sizeof(url),
             "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=zh&format=json",
             encoded_city);

    esp_err_t err = http_get_json(url, &buffer);
    if (err == ESP_OK) {
        err = parse_geocoding(&buffer, &g_weather.location.latitude, &g_weather.location.longitude);
    }
    if (err == ESP_OK) {
        err = oclock_config_save_weather(g_weather.location.city_name,
                                         g_weather.location.latitude,
                                         g_weather.location.longitude);
    }
    http_buffer_free(&buffer);
    ESP_RETURN_ON_ERROR(err, TAG, "geocoding flow failed");
    ESP_LOGI(TAG,
             "Resolved city '%s' -> lat=%.4f lon=%.4f",
             g_weather.location.city_name,
             g_weather.location.latitude,
             g_weather.location.longitude);
    g_weather.version++;
    return ESP_OK;
}

static esp_err_t fetch_forecast(void)
{
    char url[512];
    http_buffer_t buffer = {0};
    ESP_RETURN_ON_ERROR(http_buffer_init(&buffer), TAG, "forecast buffer init failed");
    snprintf(url,
             sizeof(url),
             "http://wttr.in/%.2f,%.2f?format=j1",
             g_weather.location.latitude,
             g_weather.location.longitude);

    esp_err_t err = http_get_json(url, &buffer);
    if (err == ESP_OK) {
        err = parse_forecast(&buffer);
    }
    http_buffer_free(&buffer);
    ESP_RETURN_ON_ERROR(err, TAG, "forecast flow failed");
    g_weather.last_success_ms = esp_timer_get_time() / 1000;
    g_weather.version++;
    ESP_LOGI(TAG,
             "Weather updated: temp=%.1fC humidity=%.0f%% code=%d max=%.1f min=%.1f",
             g_weather.cache.temperature_c,
             g_weather.cache.humidity_percent,
             g_weather.cache.weather_code,
             g_weather.cache.temp_max_c,
             g_weather.cache.temp_min_c);
    return ESP_OK;
}

esp_err_t oclock_weather_init(void)
{
    if (g_weather.initialized) {
        return ESP_OK;
    }

    oclock_device_config_t config = {0};
    ESP_RETURN_ON_ERROR(oclock_config_load(&config), TAG, "config load failed");

    memset(&g_weather.location, 0, sizeof(g_weather.location));
    strncpy(g_weather.location.city_name,
            config.weather_city,
            sizeof(g_weather.location.city_name) - 1);
    g_weather.location.latitude = config.weather_latitude;
    g_weather.location.longitude = config.weather_longitude;
    g_weather.location.configured = config.weather_configured;
    memset(&g_weather.cache, 0, sizeof(g_weather.cache));
    g_weather.refresh_requested = g_weather.location.configured;
    g_weather.initialized = true;
    return ESP_OK;
}

esp_err_t oclock_weather_tick(void)
{
    if (!g_weather.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    oclock_network_status_t net = {0};
    if (oclock_network_get_status(&net) != ESP_OK || !net.wifi_connected) {
        return ESP_OK;
    }

    if (!g_weather.location.configured) {
        return ESP_OK;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    bool should_attempt = g_weather.refresh_requested ||
                          (!g_weather.cache.valid && g_weather.last_attempt_ms == 0);
    if (!g_weather.cache.valid && (now_ms - g_weather.last_attempt_ms) >= kLocationRetryMs) {
        should_attempt = true;
    }
    if (g_weather.cache.valid && (now_ms - g_weather.last_success_ms) >= kWeatherRefreshMs) {
        should_attempt = true;
    }
    if (!g_weather.cache.valid && (now_ms - g_weather.last_attempt_ms) >= kRefreshRetryMs) {
        should_attempt = true;
    }
    if (!should_attempt) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Attempting weather refresh for '%s'", g_weather.location.city_name);
    g_weather.refresh_requested = false;
    g_weather.last_attempt_ms = now_ms;
    if (resolve_location_if_needed() != ESP_OK) {
        return ESP_OK;
    }
    (void)fetch_forecast();
    return ESP_OK;
}

esp_err_t oclock_weather_request_refresh(void)
{
    g_weather.refresh_requested = true;
    return ESP_OK;
}

esp_err_t oclock_weather_set_location(const char *city_name)
{
    ESP_RETURN_ON_FALSE(city_name != NULL && city_name[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "city name required");

    strncpy(g_weather.location.city_name,
            city_name,
            sizeof(g_weather.location.city_name) - 1);
    g_weather.location.latitude = 0.0;
    g_weather.location.longitude = 0.0;
    g_weather.location.configured = true;
    memset(&g_weather.cache, 0, sizeof(g_weather.cache));
    g_weather.refresh_requested = true;
    g_weather.version++;
    return oclock_config_save_weather(g_weather.location.city_name, 0.0, 0.0);
}

esp_err_t oclock_weather_get_location(oclock_weather_location_t *out_location)
{
    ESP_RETURN_ON_FALSE(out_location != NULL, ESP_ERR_INVALID_ARG, TAG, "out_location is null");
    *out_location = g_weather.location;
    return ESP_OK;
}

void oclock_weather_build_view(const oclock_datetime_t *current_time,
                               oclock_weather_view_t *out_view)
{
    oclock_network_status_t net = {0};
    bool wifi_ready = oclock_network_get_status(&net) == ESP_OK && net.wifi_connected;

    memset(out_view, 0, sizeof(*out_view));

    if (g_weather.location.configured) {
        snprintf(out_view->location_text,
                 sizeof(out_view->location_text),
                 "%s",
                 g_weather.location.city_name);

        if (g_weather.cache.valid) {
            snprintf(out_view->summary_text, sizeof(out_view->summary_text),
                     "%s", g_weather.cache.current_summary);
            snprintf(out_view->current_temp_text, sizeof(out_view->current_temp_text),
                     "%.1f\xC2\xB0""C", g_weather.cache.temperature_c);
            snprintf(out_view->humidity_text, sizeof(out_view->humidity_text),
                     "\xE6\xB9\xBF\xE5\xBA\xA6 %.0f%%", g_weather.cache.humidity_percent);
            snprintf(out_view->today_range_text, sizeof(out_view->today_range_text),
                     "\xE4\xBB\x8A\xE6\x97\xA5 %.0f / %.0f\xC2\xB0""C",
                     g_weather.cache.temp_max_c,
                     g_weather.cache.temp_min_c);

            format_offset_date(current_time, 0, out_view->today_date_text, sizeof(out_view->today_date_text));
            format_offset_date(current_time, 1, out_view->day1_date_text, sizeof(out_view->day1_date_text));
            format_offset_date(current_time, 2, out_view->day2_date_text, sizeof(out_view->day2_date_text));

            snprintf(out_view->today_summary_text, sizeof(out_view->today_summary_text),
                     "%s", g_weather.cache.current_summary);
            snprintf(out_view->day1_summary_text, sizeof(out_view->day1_summary_text),
                     "%s", g_weather.cache.daily_summary[0]);
            snprintf(out_view->day2_summary_text, sizeof(out_view->day2_summary_text),
                     "%s", g_weather.cache.daily_summary[1]);

            snprintf(out_view->today_max_text, sizeof(out_view->today_max_text),
                     "%.0f", g_weather.cache.temp_max_c);
            snprintf(out_view->today_min_text, sizeof(out_view->today_min_text),
                     "%.0f", g_weather.cache.temp_min_c);
            snprintf(out_view->day1_max_text, sizeof(out_view->day1_max_text),
                     "%.0f", g_weather.cache.daily_max_c[0]);
            snprintf(out_view->day1_min_text, sizeof(out_view->day1_min_text),
                     "%.0f", g_weather.cache.daily_min_c[0]);
            snprintf(out_view->day2_max_text, sizeof(out_view->day2_max_text),
                     "%.0f", g_weather.cache.daily_max_c[1]);
            snprintf(out_view->day2_min_text, sizeof(out_view->day2_min_text),
                     "%.0f", g_weather.cache.daily_min_c[1]);

            snprintf(out_view->day1_text, sizeof(out_view->day1_text),
                     "\xE6\x98\x8E\xE5\xA4\xA9 %s %.0f/%.0f",
                     weather_code_summary(g_weather.cache.daily_codes[0]),
                     g_weather.cache.daily_max_c[0],
                     g_weather.cache.daily_min_c[0]);
            snprintf(out_view->day2_text, sizeof(out_view->day2_text),
                     "\xE5\x90\x8E\xE5\xA4\xA9 %s %.0f/%.0f",
                     weather_code_summary(g_weather.cache.daily_codes[1]),
                     g_weather.cache.daily_max_c[1],
                     g_weather.cache.daily_min_c[1]);
            snprintf(out_view->updated_text, sizeof(out_view->updated_text),
                     "\xE6\x9B\xB4\xE6\x96\xB0 %s",
                     g_weather.cache.updated_iso);
            out_view->valid = true;
            out_view->stale =
                current_time != NULL &&
                current_time->valid &&
                g_weather.last_success_ms > 0 &&
                ((esp_timer_get_time() / 1000) - g_weather.last_success_ms) > kWeatherRefreshMs;
        } else {
            if (!wifi_ready) {
                snprintf(out_view->summary_text, sizeof(out_view->summary_text),
                         "%s", "WiFi\xE6\x9C\xAA\xE8\xBF\x9E");
                snprintf(out_view->updated_text, sizeof(out_view->updated_text),
                         "%s", "\xE7\xAD\x89\xE5\xBE\x85\xE7\xBD\x91\xE7\xBB\x9C\xE8\xBF\x9E\xE6\x8E\xA5");
            } else if (g_weather.location.latitude == 0.0 &&
                       g_weather.location.longitude == 0.0) {
                snprintf(out_view->summary_text, sizeof(out_view->summary_text),
                         "%s", "\xE8\xA7\xA3\xE6\x9E\x90\xE5\x9F\x8E\xE5\xB8\x82\xE4\xB8\xAD");
                snprintf(out_view->updated_text, sizeof(out_view->updated_text),
                         "%s", "\xE7\xAD\x89\xE5\xBE\x85\xE5\x9C\xB0\xE7\x90\x86\xE7\xBC\x96\xE7\xA0\x81");
            } else {
                snprintf(out_view->summary_text, sizeof(out_view->summary_text),
                         "%s", "\xE5\xA4\xA9\xE6\xB0\x94\xE5\xA4\xB1\xE8\xB4\xA5");
                snprintf(out_view->updated_text, sizeof(out_view->updated_text),
                         "%s", "\xE7\xAD\x89\xE5\xBE\x85\xE4\xB8\x8B\xE6\xAC\xA1\xE9\x87\x8D\xE8\xAF\x95");
            }

            snprintf(out_view->current_temp_text, sizeof(out_view->current_temp_text),
                     "--.-\xC2\xB0""C");
            snprintf(out_view->humidity_text, sizeof(out_view->humidity_text),
                     "\xE6\xB9\xBF\xE5\xBA\xA6 --%%");
            snprintf(out_view->today_range_text, sizeof(out_view->today_range_text),
                     "\xE4\xBB\x8A\xE6\x97\xA5 -- / --\xC2\xB0""C");
            snprintf(out_view->today_date_text, sizeof(out_view->today_date_text), "--/--");
            snprintf(out_view->day1_date_text, sizeof(out_view->day1_date_text), "--/--");
            snprintf(out_view->day2_date_text, sizeof(out_view->day2_date_text), "--/--");
            snprintf(out_view->today_summary_text, sizeof(out_view->today_summary_text),
                     "%s", out_view->summary_text);
            snprintf(out_view->day1_summary_text, sizeof(out_view->day1_summary_text),
                     "%s", "\xE5\xBE\x85\xE6\x9B\xB4\xE6\x96\xB0");
            snprintf(out_view->day2_summary_text, sizeof(out_view->day2_summary_text),
                     "%s", "\xE5\xBE\x85\xE6\x9B\xB4\xE6\x96\xB0");
            snprintf(out_view->today_max_text, sizeof(out_view->today_max_text), "--");
            snprintf(out_view->today_min_text, sizeof(out_view->today_min_text), "--");
            snprintf(out_view->day1_max_text, sizeof(out_view->day1_max_text), "--");
            snprintf(out_view->day1_min_text, sizeof(out_view->day1_min_text), "--");
            snprintf(out_view->day2_max_text, sizeof(out_view->day2_max_text), "--");
            snprintf(out_view->day2_min_text, sizeof(out_view->day2_min_text), "--");
            snprintf(out_view->day1_text, sizeof(out_view->day1_text),
                     "%s", "\xE6\x98\x8E\xE5\xA4\xA9\xE5\xBE\x85\xE6\x9B\xB4\xE6\x96\xB0");
            snprintf(out_view->day2_text, sizeof(out_view->day2_text),
                     "%s", "\xE5\x90\x8E\xE5\xA4\xA9\xE5\xBE\x85\xE6\x9B\xB4\xE6\x96\xB0");
            out_view->valid = false;
            out_view->stale = true;
        }
    } else {
        snprintf(out_view->location_text, sizeof(out_view->location_text),
                 "%s", "\xE6\x9C\xAA\xE8\xAE\xBE\xE7\xBD\xAE\xE5\x9F\x8E\xE5\xB8\x82");
        snprintf(out_view->summary_text, sizeof(out_view->summary_text),
                 "%s", "\xE5\xA4\xA9\xE6\xB0\x94\xE6\x9C\xAA\xE9\x85\x8D\xE7\xBD\xAE");
        snprintf(out_view->updated_text, sizeof(out_view->updated_text),
                 "%s", "\xE7\xBD\x91\xE9\xA1\xB5\xE8\xAE\xBE\xE7\xBD\xAE\xE5\x9F\x8E\xE5\xB8\x82");
        snprintf(out_view->current_temp_text, sizeof(out_view->current_temp_text),
                 "--.-\xC2\xB0""C");
        snprintf(out_view->humidity_text, sizeof(out_view->humidity_text),
                 "\xE6\xB9\xBF\xE5\xBA\xA6 --%%");
        snprintf(out_view->today_range_text, sizeof(out_view->today_range_text),
                 "\xE4\xBB\x8A\xE6\x97\xA5 -- / --\xC2\xB0""C");
        snprintf(out_view->today_date_text, sizeof(out_view->today_date_text), "--/--");
        snprintf(out_view->day1_date_text, sizeof(out_view->day1_date_text), "--/--");
        snprintf(out_view->day2_date_text, sizeof(out_view->day2_date_text), "--/--");
        snprintf(out_view->today_summary_text, sizeof(out_view->today_summary_text), "--");
        snprintf(out_view->day1_summary_text, sizeof(out_view->day1_summary_text), "--");
        snprintf(out_view->day2_summary_text, sizeof(out_view->day2_summary_text), "--");
        snprintf(out_view->today_max_text, sizeof(out_view->today_max_text), "--");
        snprintf(out_view->today_min_text, sizeof(out_view->today_min_text), "--");
        snprintf(out_view->day1_max_text, sizeof(out_view->day1_max_text), "--");
        snprintf(out_view->day1_min_text, sizeof(out_view->day1_min_text), "--");
        snprintf(out_view->day2_max_text, sizeof(out_view->day2_max_text), "--");
        snprintf(out_view->day2_min_text, sizeof(out_view->day2_min_text), "--");
        snprintf(out_view->day1_text, sizeof(out_view->day1_text), "--");
        snprintf(out_view->day2_text, sizeof(out_view->day2_text), "--");
        out_view->valid = false;
        out_view->stale = true;
    }
}

uint32_t oclock_weather_get_version(void)
{
    return g_weather.version;
}
