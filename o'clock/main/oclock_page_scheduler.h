#ifndef OCLOCK_PAGE_SCHEDULER_H
#define OCLOCK_PAGE_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include "oclock_pages.h"

typedef struct {
    oclock_page_id_t current_page;
    int64_t last_auto_transition_ms;
    int64_t manual_pause_until_ms;
    int64_t refresh_feedback_until_ms;
    bool refresh_requested;
} oclock_page_scheduler_t;

typedef struct {
    oclock_page_id_t current_page;
    bool auto_rotation_paused;
    bool refresh_feedback_active;
    bool refresh_requested;
    int64_t manual_pause_remaining_ms;
    int64_t refresh_feedback_remaining_ms;
} oclock_page_scheduler_snapshot_t;

void oclock_page_scheduler_init(oclock_page_scheduler_t *scheduler,
                                int64_t now_ms);
bool oclock_page_scheduler_tick(oclock_page_scheduler_t *scheduler,
                                int64_t now_ms);
bool oclock_page_scheduler_on_short_press(oclock_page_scheduler_t *scheduler,
                                          int64_t now_ms);
bool oclock_page_scheduler_on_long_press(oclock_page_scheduler_t *scheduler,
                                         int64_t now_ms);
void oclock_page_scheduler_get_snapshot(
    const oclock_page_scheduler_t *scheduler,
    int64_t now_ms,
    oclock_page_scheduler_snapshot_t *snapshot);
bool oclock_page_scheduler_take_refresh_request(
    oclock_page_scheduler_t *scheduler);

#endif
