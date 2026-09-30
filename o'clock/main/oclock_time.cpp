#include "oclock_time.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "oclock_board_config.h"
#include "oclock_i2c_bus.h"

#include "SensorPCF85063.hpp"

namespace {

static constexpr uint8_t kRtcAddress = 0x51;
static const char *TAG = "oclock_time";

struct OclockRtcContext {
    i2c_master_dev_handle_t dev_handle = nullptr;
    SensorPCF85063 rtc;
    bool ready = false;
    bool ntp_synced = false;
};

OclockRtcContext g_rtc = {};

bool rtc_callback(uint8_t address,
                  uint8_t reg,
                  uint8_t *buf,
                  size_t len,
                  bool write_reg,
                  bool is_write)
{
    (void)address;

    if (g_rtc.dev_handle == nullptr) {
        return false;
    }

    int ret = ESP_OK;
    if (is_write) {
        ret = oclock_i2c_write_register(g_rtc.dev_handle, write_reg ? reg : -1, buf, (uint8_t)len);
    } else {
        ret = oclock_i2c_read_register(g_rtc.dev_handle, write_reg ? reg : -1, buf, (uint8_t)len);
    }
    return ret == ESP_OK;
}

bool rtc_datetime_plausible(const RTC_DateTime &dt)
{
    return dt.getYear() >= 2020 && dt.getYear() <= 2099 &&
           dt.getMonth() >= 1 && dt.getMonth() <= 12 &&
           dt.getDay() >= 1 && dt.getDay() <= 31 &&
           dt.getHour() <= 23 &&
           dt.getMinute() <= 59 &&
           dt.getSecond() <= 59;
}

RTC_DateTime build_datetime()
{
    return RTC_DateTime(__DATE__, __TIME__);
}

bool rtc_matches_previous_placeholder_seed(const RTC_DateTime &dt)
{
    return dt.getYear() == 2026 &&
           dt.getMonth() == 6 &&
           dt.getDay() == 1 &&
           dt.getHour() == 12 &&
           dt.getMinute() == 0 &&
           dt.getSecond() == 0;
}

int rtc_compare_datetime(const RTC_DateTime &lhs, const RTC_DateTime &rhs)
{
    if (lhs.getYear() != rhs.getYear()) {
        return lhs.getYear() < rhs.getYear() ? -1 : 1;
    }
    if (lhs.getMonth() != rhs.getMonth()) {
        return lhs.getMonth() < rhs.getMonth() ? -1 : 1;
    }
    if (lhs.getDay() != rhs.getDay()) {
        return lhs.getDay() < rhs.getDay() ? -1 : 1;
    }
    if (lhs.getHour() != rhs.getHour()) {
        return lhs.getHour() < rhs.getHour() ? -1 : 1;
    }
    if (lhs.getMinute() != rhs.getMinute()) {
        return lhs.getMinute() < rhs.getMinute() ? -1 : 1;
    }
    if (lhs.getSecond() != rhs.getSecond()) {
        return lhs.getSecond() < rhs.getSecond() ? -1 : 1;
    }
    return 0;
}

const char *weekday_name_short(int weekday)
{
    switch (weekday) {
    case 0:
        return "Sun";
    case 1:
        return "Mon";
    case 2:
        return "Tue";
    case 3:
        return "Wed";
    case 4:
        return "Thu";
    case 5:
        return "Fri";
    case 6:
        return "Sat";
    default:
        return "?";
    }
}

} // namespace

extern "C" esp_err_t oclock_time_init(void)
{
    if (g_rtc.ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(oclock_i2c_bus_add_device(kRtcAddress, 300000, &g_rtc.dev_handle),
                        TAG,
                        "rtc i2c device add failed");

    ESP_RETURN_ON_FALSE(g_rtc.rtc.begin(rtc_callback),
                        ESP_FAIL,
                        TAG,
                        "rtc begin failed");

    RTC_DateTime dt = g_rtc.rtc.getDateTime();
    const RTC_DateTime compile_dt = build_datetime();
    const bool older_than_build_time =
        rtc_datetime_plausible(dt) && rtc_compare_datetime(dt, compile_dt) < 0;
    if (!rtc_datetime_plausible(dt) ||
        rtc_matches_previous_placeholder_seed(dt) ||
        older_than_build_time) {
        ESP_LOGW(TAG,
                 "RTC data needs seeding, writing firmware build time baseline");
        g_rtc.rtc.setDateTime(compile_dt);
        dt = g_rtc.rtc.getDateTime();
    }

    ESP_LOGI(TAG,
             "RTC ready: %04d-%02d-%02d %02d:%02d:%02d %s",
             dt.getYear(),
             dt.getMonth(),
             dt.getDay(),
             dt.getHour(),
             dt.getMinute(),
             dt.getSecond(),
             weekday_name_short(dt.getWeek()));

    g_rtc.ready = true;
    return ESP_OK;
}

extern "C" esp_err_t oclock_time_get_current(oclock_datetime_t *out_time)
{
    ESP_RETURN_ON_FALSE(out_time != nullptr, ESP_ERR_INVALID_ARG, TAG, "out_time is null");

    if (!g_rtc.ready) {
        out_time->valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    RTC_DateTime dt = g_rtc.rtc.getDateTime();
    out_time->year = dt.getYear();
    out_time->month = dt.getMonth();
    out_time->day = dt.getDay();
    out_time->hour = dt.getHour();
    out_time->minute = dt.getMinute();
    out_time->second = dt.getSecond();
    out_time->weekday = dt.getWeek();
    out_time->valid = rtc_datetime_plausible(dt);
    out_time->sourced_from_rtc = true;
    out_time->synced_from_network = g_rtc.ntp_synced;
    return out_time->valid ? ESP_OK : ESP_FAIL;
}

extern "C" bool oclock_time_is_ready(void)
{
    return g_rtc.ready;
}

extern "C" esp_err_t oclock_time_mark_ntp_synced(void)
{
    time_t now = 0;
    struct tm local_time = {};

    time(&now);
    localtime_r(&now, &local_time);
    g_rtc.rtc.setDateTime((uint16_t)(local_time.tm_year + 1900),
                          (uint8_t)(local_time.tm_mon + 1),
                          (uint8_t)local_time.tm_mday,
                          (uint8_t)local_time.tm_hour,
                          (uint8_t)local_time.tm_min,
                          (uint8_t)local_time.tm_sec);
    g_rtc.ntp_synced = true;
    ESP_LOGI(TAG, "RTC updated from synchronized system time");
    return ESP_OK;
}
