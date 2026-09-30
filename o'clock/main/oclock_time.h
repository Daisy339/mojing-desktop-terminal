#ifndef OCLOCK_TIME_H
#define OCLOCK_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
    bool valid;
    bool sourced_from_rtc;
    bool synced_from_network;
} oclock_datetime_t;

esp_err_t oclock_time_init(void);
esp_err_t oclock_time_get_current(oclock_datetime_t *out_time);
bool oclock_time_is_ready(void);
esp_err_t oclock_time_mark_ntp_synced(void);

#ifdef __cplusplus
}
#endif

#endif
