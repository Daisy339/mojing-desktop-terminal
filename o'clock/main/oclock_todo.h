#ifndef OCLOCK_TODO_H
#define OCLOCK_TODO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "oclock_time.h"

#define OCLOCK_TODO_MAX_ITEMS 16
#define OCLOCK_TODO_TITLE_MAX_LEN 64
#define OCLOCK_TODO_DISPLAY_MAX 5

typedef enum {
    OCLOCK_TODO_PRIORITY_LOW = 1,
    OCLOCK_TODO_PRIORITY_MEDIUM = 2,
    OCLOCK_TODO_PRIORITY_HIGH = 3,
} oclock_todo_priority_t;

typedef struct {
    uint32_t id;
    char title[OCLOCK_TODO_TITLE_MAX_LEN];
    uint16_t due_year;
    uint8_t due_month;
    uint8_t due_day;
    uint8_t priority;
    bool completed;
} oclock_todo_item_t;

typedef struct {
    char title[OCLOCK_TODO_TITLE_MAX_LEN];
    char due_text[20];
    char priority_text[8];
    bool overdue;
} oclock_todo_display_entry_t;

typedef struct {
    size_t count;
    uint32_t total_items;
    uint32_t open_items;
    uint32_t completed_items;
    oclock_todo_display_entry_t entries[OCLOCK_TODO_DISPLAY_MAX];
} oclock_todo_view_t;

esp_err_t oclock_todo_init(void);
esp_err_t oclock_todo_create(const char *title,
                             const char *due_date,
                             const char *priority_text);
esp_err_t oclock_todo_update(uint32_t id,
                             const char *title,
                             const char *due_date,
                             const char *priority_text);
esp_err_t oclock_todo_set_completed(uint32_t id, bool completed);
esp_err_t oclock_todo_delete(uint32_t id);
size_t oclock_todo_list_all(oclock_todo_item_t *items, size_t max_items);
void oclock_todo_build_view(const oclock_datetime_t *current_time,
                            oclock_todo_view_t *view);
uint32_t oclock_todo_get_version(void);
bool oclock_todo_is_overdue(const oclock_todo_item_t *item,
                            const oclock_datetime_t *current_time);
const char *oclock_todo_priority_label(uint8_t priority);

#endif
