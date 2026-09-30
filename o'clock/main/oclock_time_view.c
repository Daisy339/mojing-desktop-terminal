#include "oclock_time_view.h"

#include <stdio.h>
#include <string.h>

static const char *oclock_weekday_name(int weekday)
{
    switch (weekday) {
    case 0:
        return "星期日";
    case 1:
        return "星期一";
    case 2:
        return "星期二";
    case 3:
        return "星期三";
    case 4:
        return "星期四";
    case 5:
        return "星期五";
    case 6:
        return "星期六";
    default:
        return "未知";
    }
}

void oclock_time_build_view(const oclock_datetime_t *datetime,
                            oclock_time_view_t *view)
{
    if (datetime == NULL || view == NULL || !datetime->valid) {
        if (view != NULL) {
            snprintf(view->time_text, sizeof(view->time_text), "--:--");
            snprintf(view->date_text, sizeof(view->date_text), "日期不可用");
            snprintf(view->weekday_text, sizeof(view->weekday_text), "未知");
            snprintf(view->source_text, sizeof(view->source_text), "时钟不可用");
            view->valid = false;
        }
        return;
    }

    snprintf(view->time_text,
             sizeof(view->time_text),
             "%02d:%02d",
             datetime->hour,
             datetime->minute);
    snprintf(view->date_text,
             sizeof(view->date_text),
             "%04d年%02d月%02d日",
             datetime->year,
             datetime->month,
             datetime->day);
    snprintf(view->weekday_text,
             sizeof(view->weekday_text),
             "%s",
             oclock_weekday_name(datetime->weekday));
    snprintf(view->source_text,
             sizeof(view->source_text),
             "%s",
             datetime->synced_from_network ? "网络校时" :
             (datetime->sourced_from_rtc ? "RTC时钟" : "系统时钟"));
    view->valid = true;
}
