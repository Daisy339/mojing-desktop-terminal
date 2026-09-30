#ifndef OCLOCK_TIME_VIEW_H
#define OCLOCK_TIME_VIEW_H

#include <stdbool.h>

#include "oclock_time.h"

typedef struct {
    char time_text[6];
    char date_text[24];
    char weekday_text[12];
    char source_text[24];
    bool valid;
} oclock_time_view_t;

void oclock_time_build_view(const oclock_datetime_t *datetime,
                            oclock_time_view_t *view);

#endif
