#ifndef OCLOCK_NETWORK_H
#define OCLOCK_NETWORK_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    OCLOCK_NETWORK_MODE_NONE = 0,
    OCLOCK_NETWORK_MODE_STA,
    OCLOCK_NETWORK_MODE_AP,
} oclock_network_mode_t;

typedef struct {
    oclock_network_mode_t mode;
    bool wifi_connected;
    bool ap_active;
    bool ntp_synced;
    char ip_address[16];
    char ap_ssid[33];
    char sta_ssid[33];
} oclock_network_status_t;

esp_err_t oclock_network_init(void);
esp_err_t oclock_network_start(void);
esp_err_t oclock_network_force_ap_mode(void);
esp_err_t oclock_network_tick(void);
esp_err_t oclock_network_set_wifi_credentials(const char *ssid, const char *password);
esp_err_t oclock_network_apply_saved_wifi(void);
esp_err_t oclock_network_get_status(oclock_network_status_t *out_status);

#endif
