#include "oclock_page_scheduler.h"

#define OCLOCK_AUTO_ROTATE_INTERVAL_MS 12000
#define OCLOCK_MANUAL_PAUSE_MS 30000
#define OCLOCK_REFRESH_FEEDBACK_MS 3000

static bool oclock_page_scheduler_pause_active(
    const oclock_page_scheduler_t *scheduler,
    int64_t now_ms)
{
    return now_ms < scheduler->manual_pause_until_ms;
}

static bool oclock_page_scheduler_refresh_active(
    const oclock_page_scheduler_t *scheduler,
    int64_t now_ms)
{
    return now_ms < scheduler->refresh_feedback_until_ms;
}

void oclock_page_scheduler_init(oclock_page_scheduler_t *scheduler,
                                int64_t now_ms)
{
    scheduler->current_page = OCLOCK_PAGE_HOME;
    scheduler->last_auto_transition_ms = now_ms;
    scheduler->manual_pause_until_ms = 0;
    scheduler->refresh_feedback_until_ms = 0;
    scheduler->refresh_requested = false;
}

bool oclock_page_scheduler_tick(oclock_page_scheduler_t *scheduler,
                                int64_t now_ms)
{
    if (oclock_page_scheduler_pause_active(scheduler, now_ms)) {
        return false;
    }

    if (!oclock_page_is_auto_rotation_member(scheduler->current_page)) {
        scheduler->current_page = OCLOCK_PAGE_HOME;
        scheduler->last_auto_transition_ms = now_ms;
        return true;
    }

    if ((now_ms - scheduler->last_auto_transition_ms) <
        OCLOCK_AUTO_ROTATE_INTERVAL_MS) {
        return false;
    }

    scheduler->current_page = oclock_page_next_auto(scheduler->current_page);
    scheduler->last_auto_transition_ms = now_ms;
    return true;
}

bool oclock_page_scheduler_on_short_press(oclock_page_scheduler_t *scheduler,
                                          int64_t now_ms)
{
    oclock_page_id_t next_page =
        oclock_page_next_manual(scheduler->current_page);

    scheduler->current_page = next_page;
    scheduler->manual_pause_until_ms = now_ms + OCLOCK_MANUAL_PAUSE_MS;
    scheduler->last_auto_transition_ms = now_ms;
    return true;
}

bool oclock_page_scheduler_on_long_press(oclock_page_scheduler_t *scheduler,
                                         int64_t now_ms)
{
    scheduler->manual_pause_until_ms = now_ms + OCLOCK_REFRESH_FEEDBACK_MS;
    scheduler->refresh_feedback_until_ms = now_ms + OCLOCK_REFRESH_FEEDBACK_MS;
    scheduler->last_auto_transition_ms = now_ms;
    scheduler->refresh_requested = true;
    return true;
}

void oclock_page_scheduler_get_snapshot(
    const oclock_page_scheduler_t *scheduler,
    int64_t now_ms,
    oclock_page_scheduler_snapshot_t *snapshot)
{
    snapshot->current_page = scheduler->current_page;
    snapshot->auto_rotation_paused =
        oclock_page_scheduler_pause_active(scheduler, now_ms);
    snapshot->refresh_feedback_active =
        oclock_page_scheduler_refresh_active(scheduler, now_ms);
    snapshot->refresh_requested = scheduler->refresh_requested;
    snapshot->manual_pause_remaining_ms =
        snapshot->auto_rotation_paused ? scheduler->manual_pause_until_ms - now_ms
                                       : 0;
    snapshot->refresh_feedback_remaining_ms =
        snapshot->refresh_feedback_active
            ? scheduler->refresh_feedback_until_ms - now_ms
            : 0;
}

bool oclock_page_scheduler_take_refresh_request(
    oclock_page_scheduler_t *scheduler)
{
    bool refresh_requested = scheduler->refresh_requested;
    scheduler->refresh_requested = false;
    return refresh_requested;
}
