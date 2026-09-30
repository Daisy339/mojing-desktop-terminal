#include "oclock_network.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

#include "oclock_board_config.h"
#include "oclock_config.h"
#include "oclock_time.h"

static const char *TAG = "oclock_network";
static const char *kApSsid = "OClock-Terminal-Setup";
static const char *kApPassword = "12345678";
static const int kApChannel = 1;
static const int kApMaxConn = 4;

typedef struct {
    bool initialized;
    bool started;
    oclock_device_config_t config;
    oclock_network_status_t status;
    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    bool ntp_started;
    int64_t last_sta_retry_ms;
    int64_t sta_disconnect_started_ms;
    int64_t last_status_log_ms;
    uint32_t sta_retry_count;
    uint32_t status_version;
} oclock_network_context_t;

static oclock_network_context_t g_network = {0};
static const int64_t kStaFallbackDelayMs = 15000;
static const uint32_t kStaRetryLimit = 3;
static const int64_t kApRetryIntervalMs = 10000;
static const int64_t kStatusLogIntervalMs = 5000;

static const char *oclock_network_disconnect_reason_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_BEACON_TIMEOUT:
        return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL:
        return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL:
        return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4WAY_TIMEOUT";
    default:
        return "UNKNOWN";
    }
}

static int64_t oclock_network_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void oclock_network_bump_status(void)
{
    g_network.status_version++;
}

static const char *oclock_network_mode_name(oclock_network_mode_t mode)
{
    switch (mode) {
    case OCLOCK_NETWORK_MODE_STA:
        return "sta";
    case OCLOCK_NETWORK_MODE_AP:
        return "ap";
    case OCLOCK_NETWORK_MODE_NONE:
    default:
        return "none";
    }
}

static void oclock_network_log_status(const char *reason)
{
    ESP_LOGI(TAG,
             "[%s] mode=%s wifi=%s ap=%s ntp=%s ip=%s sta_ssid=%s ap_ssid=%s",
             reason,
             oclock_network_mode_name(g_network.status.mode),
             g_network.status.wifi_connected ? "true" : "false",
             g_network.status.ap_active ? "true" : "false",
             g_network.status.ntp_synced ? "true" : "false",
             g_network.status.ip_address[0] ? g_network.status.ip_address : "-",
             g_network.status.sta_ssid[0] ? g_network.status.sta_ssid : "-",
             g_network.status.ap_ssid[0] ? g_network.status.ap_ssid : "-");
    g_network.last_status_log_ms = oclock_network_now_ms();
}

static void oclock_network_apply_timezone(void)
{
    setenv("TZ", OCLOCK_TIMEZONE, 1);
    tzset();
}

static void oclock_network_sntp_cb(struct timeval *tv)
{
    (void)tv;
    oclock_network_apply_timezone();
    g_network.status.ntp_synced = true;
    oclock_time_mark_ntp_synced();
    oclock_network_bump_status();
    ESP_LOGI(TAG, "SNTP sync complete");
}

static void oclock_network_start_sntp(void)
{
    if (g_network.ntp_started) {
        return;
    }
    oclock_network_apply_timezone();
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, OCLOCK_NTP_SERVER);
    sntp_set_time_sync_notification_cb(oclock_network_sntp_cb);
    esp_sntp_init();
    g_network.ntp_started = true;
    ESP_LOGI(TAG, "SNTP started with server %s", OCLOCK_NTP_SERVER);
}

static void oclock_network_set_ip_from_event(const ip_event_got_ip_t *event)
{
    snprintf(g_network.status.ip_address,
             sizeof(g_network.status.ip_address),
             IPSTR,
             IP2STR(&event->ip_info.ip));
}

static esp_err_t oclock_network_start_sta(bool keep_ap_active);

static void oclock_network_event_handler(void *arg,
                                         esp_event_base_t event_base,
                                         int32_t event_id,
                                         void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        g_network.status.mode =
            g_network.status.ap_active ? OCLOCK_NETWORK_MODE_AP : OCLOCK_NETWORK_MODE_STA;
        g_network.status.wifi_connected = false;
        g_network.status.ntp_synced = false;
        g_network.ntp_started = false;
        g_network.sta_disconnect_started_ms = oclock_network_now_ms();
        g_network.sta_retry_count = 0;
        oclock_network_bump_status();
        oclock_network_log_status("sta_start");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;
        g_network.status.wifi_connected = false;
        g_network.status.ip_address[0] = '\0';
        if (g_network.sta_disconnect_started_ms == 0) {
            g_network.sta_disconnect_started_ms = oclock_network_now_ms();
        }
        g_network.sta_retry_count++;
        oclock_network_bump_status();
        ESP_LOGW(TAG,
                 "STA disconnected, reason=%u (%s), retry %lu/%lu",
                 event->reason,
                 oclock_network_disconnect_reason_name(event->reason),
                 (unsigned long)g_network.sta_retry_count,
                 (unsigned long)kStaRetryLimit);
        oclock_network_log_status("sta_disconnected");
        if (g_network.sta_retry_count < kStaRetryLimit) {
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        g_network.status.mode = OCLOCK_NETWORK_MODE_STA;
        g_network.status.wifi_connected = true;
        oclock_network_set_ip_from_event(event);
        g_network.sta_disconnect_started_ms = 0;
        g_network.sta_retry_count = 0;
        if (g_network.status.ap_active) {
            ESP_LOGI(TAG, "STA connected while AP active, disabling AP");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
            g_network.status.ap_active = false;
        }
        oclock_network_bump_status();
        ESP_LOGI(TAG, "STA got IP: %s", g_network.status.ip_address);
        oclock_network_log_status("sta_got_ip");
        oclock_network_start_sntp();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        g_network.status.mode = OCLOCK_NETWORK_MODE_AP;
        g_network.status.ap_active = true;
        g_network.status.wifi_connected = false;
        strncpy(g_network.status.ip_address, "192.168.4.1", sizeof(g_network.status.ip_address) - 1);
        oclock_network_bump_status();
        ESP_LOGI(TAG, "AP started at %s", g_network.status.ip_address);
        oclock_network_log_status("ap_start");
    }
}

static esp_err_t oclock_network_init_stack(void)
{
    if (g_network.initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(oclock_config_init(), TAG, "config init failed");
    ESP_RETURN_ON_ERROR(oclock_config_load(&g_network.config), TAG, "config load failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif init failed");

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    g_network.sta_netif = esp_netif_create_default_wifi_sta();
    g_network.ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,
                                                   ESP_EVENT_ANY_ID,
                                                   &oclock_network_event_handler,
                                                   NULL),
                        TAG,
                        "wifi event register failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,
                                                   IP_EVENT_STA_GOT_IP,
                                                   &oclock_network_event_handler,
                                                   NULL),
                        TAG,
                        "ip event register failed");

    g_network.initialized = true;
    return ESP_OK;
}

static esp_err_t oclock_network_start_ap(void)
{
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.ap.ssid,
            kApSsid,
            sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(kApSsid);
    strncpy((char *)wifi_config.ap.password,
            kApPassword,
            sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.channel = kApChannel;
    wifi_config.ap.max_connection = kApMaxConn;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = true;

    strncpy(g_network.status.ap_ssid, kApSsid, sizeof(g_network.status.ap_ssid) - 1);
    g_network.status.mode = OCLOCK_NETWORK_MODE_AP;
    g_network.status.ap_active = true;
    g_network.status.wifi_connected = false;
    g_network.status.ntp_synced = false;
    g_network.ntp_started = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "set AP config failed");
    if (!g_network.started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP failed");
    }
    g_network.started = true;
    g_network.last_sta_retry_ms = oclock_network_now_ms();
    oclock_network_log_status("start_ap");
    return ESP_OK;
}

static esp_err_t oclock_network_start_sta(bool keep_ap_active)
{
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid,
            g_network.config.wifi_ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password,
            g_network.config.wifi_password,
            sizeof(wifi_config.sta.password) - 1);

    strncpy(g_network.status.sta_ssid,
            g_network.config.wifi_ssid,
            sizeof(g_network.status.sta_ssid) - 1);
    g_network.status.mode =
        keep_ap_active ? OCLOCK_NETWORK_MODE_AP : OCLOCK_NETWORK_MODE_STA;
    g_network.status.ap_active = keep_ap_active;
    g_network.status.wifi_connected = false;
    g_network.status.ip_address[0] = '\0';
    g_network.status.ntp_synced = false;
    g_network.ntp_started = false;
    g_network.sta_disconnect_started_ms = oclock_network_now_ms();
    g_network.sta_retry_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(keep_ap_active ? WIFI_MODE_APSTA : WIFI_MODE_STA),
                        TAG,
                        "set STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set STA config failed");
    if (!g_network.started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start STA failed");
    }
    esp_err_t connect_ret = esp_wifi_connect();
    if (connect_ret != ESP_OK && connect_ret != ESP_ERR_WIFI_CONN) {
        ESP_RETURN_ON_ERROR(connect_ret, TAG, "wifi connect failed");
    }
    oclock_network_bump_status();
    g_network.started = true;
    g_network.last_sta_retry_ms = oclock_network_now_ms();
    oclock_network_log_status(keep_ap_active ? "start_apsta" : "start_sta");
    return ESP_OK;
}

esp_err_t oclock_network_init(void)
{
    return oclock_network_init_stack();
}

esp_err_t oclock_network_start(void)
{
    ESP_RETURN_ON_ERROR(oclock_network_init_stack(), TAG, "stack init failed");

    if (g_network.started) {
        return ESP_OK;
    }

    esp_err_t ret;
    if (g_network.config.wifi_configured) {
        ret = oclock_network_start_sta(false);
    } else {
        ret = oclock_network_start_ap();
    }
    return ret;
}

esp_err_t oclock_network_force_ap_mode(void)
{
    ESP_RETURN_ON_ERROR(oclock_network_init_stack(), TAG, "stack init failed");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
    g_network.started = false;
    return oclock_network_start_ap();
}

esp_err_t oclock_network_set_wifi_credentials(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid != NULL, ESP_ERR_INVALID_ARG, TAG, "ssid is null");
    ESP_RETURN_ON_FALSE(password != NULL, ESP_ERR_INVALID_ARG, TAG, "password is null");
    ESP_RETURN_ON_ERROR(oclock_config_save_wifi(ssid, password), TAG, "save wifi failed");
    ESP_RETURN_ON_ERROR(oclock_config_load(&g_network.config), TAG, "reload config failed");
    ESP_LOGI(TAG,
             "Saved Wi-Fi credentials for ssid='%s' (password length=%u)",
             g_network.config.wifi_ssid,
             (unsigned)strlen(g_network.config.wifi_password));
    oclock_network_bump_status();
    oclock_network_log_status("wifi_saved");
    return ESP_OK;
}

esp_err_t oclock_network_apply_saved_wifi(void)
{
    ESP_RETURN_ON_ERROR(oclock_config_load(&g_network.config), TAG, "reload config failed");
    if (!g_network.config.wifi_configured) {
        return ESP_ERR_INVALID_STATE;
    }

    oclock_network_bump_status();
    oclock_network_log_status("apply_saved_wifi");
    if (g_network.status.ap_active) {
        return oclock_network_start_sta(true);
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
    g_network.started = false;
    g_network.status.ap_active = false;
    g_network.status.wifi_connected = false;
    g_network.status.ip_address[0] = '\0';
    g_network.status.mode = OCLOCK_NETWORK_MODE_STA;
    return oclock_network_start_sta(false);
}

esp_err_t oclock_network_get_status(oclock_network_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "out_status is null");
    *out_status = g_network.status;
    return ESP_OK;
}

esp_err_t oclock_network_tick(void)
{
    if (!g_network.started) {
        return ESP_OK;
    }

    if (g_network.status.mode == OCLOCK_NETWORK_MODE_AP &&
        g_network.config.wifi_configured &&
        !g_network.status.wifi_connected) {
        const int64_t now_ms = oclock_network_now_ms();
        if ((now_ms - g_network.last_sta_retry_ms) >= kApRetryIntervalMs) {
            ESP_LOGI(TAG, "AP fallback retry: attempting STA reconnect");
            g_network.last_sta_retry_ms = now_ms;
            return oclock_network_start_sta(true);
        }
    }

    if (g_network.status.mode == OCLOCK_NETWORK_MODE_STA &&
        !g_network.status.wifi_connected &&
        g_network.config.wifi_configured &&
        g_network.sta_disconnect_started_ms > 0) {
        const int64_t now_ms = oclock_network_now_ms();
        if ((now_ms - g_network.sta_disconnect_started_ms) >= kStaFallbackDelayMs ||
            g_network.sta_retry_count >= kStaRetryLimit) {
            ESP_LOGI(TAG, "STA fallback timeout reached, switching to AP mode");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
            g_network.started = false;
            g_network.last_sta_retry_ms = now_ms;
            return oclock_network_start_ap();
        }
    }

    if ((oclock_network_now_ms() - g_network.last_status_log_ms) >= kStatusLogIntervalMs) {
        oclock_network_log_status("tick");
    }

    return ESP_OK;
}
