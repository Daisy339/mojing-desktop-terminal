#include "oclock_todo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "nvs.h"

static const char *TAG = "oclock_todo";
static const char *kNamespace = "oclock_todo";
static const char *kStoreKey = "store";
static const uint32_t kStoreMagic = 0x544F444FUL;

typedef struct {
    uint32_t magic;
    uint32_t next_id;
    uint32_t count;
    oclock_todo_item_t items[OCLOCK_TODO_MAX_ITEMS];
} oclock_todo_store_t;

static struct {
    oclock_todo_store_t store;
    uint32_t version;
    bool initialized;
} g_todo = {0};

static int compare_due_date(const oclock_todo_item_t *lhs,
                            const oclock_todo_item_t *rhs)
{
    if (lhs->due_year != rhs->due_year) {
        return lhs->due_year < rhs->due_year ? -1 : 1;
    }
    if (lhs->due_month != rhs->due_month) {
        return lhs->due_month < rhs->due_month ? -1 : 1;
    }
    if (lhs->due_day != rhs->due_day) {
        return lhs->due_day < rhs->due_day ? -1 : 1;
    }
    return 0;
}

static int compare_priority(uint8_t lhs, uint8_t rhs)
{
    if (lhs == rhs) {
        return 0;
    }
    return lhs > rhs ? -1 : 1;
}

static int compare_items_for_display(const void *lhs_ptr, const void *rhs_ptr)
{
    const oclock_todo_item_t *lhs = (const oclock_todo_item_t *)lhs_ptr;
    const oclock_todo_item_t *rhs = (const oclock_todo_item_t *)rhs_ptr;

    if (lhs->completed != rhs->completed) {
        return lhs->completed ? 1 : -1;
    }

    const int due_cmp = compare_due_date(lhs, rhs);
    if (due_cmp != 0) {
        return due_cmp;
    }

    const int prio_cmp = compare_priority(lhs->priority, rhs->priority);
    if (prio_cmp != 0) {
        return prio_cmp;
    }

    return lhs->id < rhs->id ? -1 : (lhs->id > rhs->id ? 1 : 0);
}

static esp_err_t persist_store(void)
{
    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle,
                                     kStoreKey,
                                     &g_todo.store,
                                     sizeof(g_todo.store)),
                        TAG,
                        "nvs_set_blob failed");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "nvs_commit failed");
    nvs_close(handle);
    g_todo.version++;
    return ESP_OK;
}

static esp_err_t load_store(void)
{
    memset(&g_todo.store, 0, sizeof(g_todo.store));
    g_todo.store.magic = kStoreMagic;
    g_todo.store.next_id = 1;

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs_open failed");

    size_t size = sizeof(g_todo.store);
    ret = nvs_get_blob(handle, kStoreKey, &g_todo.store, &size);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        g_todo.store.magic = kStoreMagic;
        g_todo.store.next_id = 1;
        g_todo.store.count = 0;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs_get_blob failed");

    if (g_todo.store.magic != kStoreMagic || g_todo.store.count > OCLOCK_TODO_MAX_ITEMS) {
        memset(&g_todo.store, 0, sizeof(g_todo.store));
        g_todo.store.magic = kStoreMagic;
        g_todo.store.next_id = 1;
        g_todo.store.count = 0;
    }
    return ESP_OK;
}

static bool parse_due_date(const char *due_date,
                           uint16_t *year,
                           uint8_t *month,
                           uint8_t *day)
{
    if (due_date == NULL) {
        return false;
    }

    int y = 0;
    int m = 0;
    int d = 0;
    if (sscanf(due_date, "%d-%d-%d", &y, &m, &d) != 3) {
        return false;
    }
    if (y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) {
        return false;
    }

    *year = (uint16_t)y;
    *month = (uint8_t)m;
    *day = (uint8_t)d;
    return true;
}

static uint8_t parse_priority(const char *priority_text)
{
    if (priority_text == NULL) {
        return OCLOCK_TODO_PRIORITY_MEDIUM;
    }
    if (strcmp(priority_text, "high") == 0) {
        return OCLOCK_TODO_PRIORITY_HIGH;
    }
    if (strcmp(priority_text, "low") == 0) {
        return OCLOCK_TODO_PRIORITY_LOW;
    }
    return OCLOCK_TODO_PRIORITY_MEDIUM;
}

static oclock_todo_item_t *find_item(uint32_t id)
{
    for (uint32_t i = 0; i < g_todo.store.count; ++i) {
        if (g_todo.store.items[i].id == id) {
            return &g_todo.store.items[i];
        }
    }
    return NULL;
}

esp_err_t oclock_todo_init(void)
{
    if (g_todo.initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(load_store(), TAG, "load_store failed");
    g_todo.initialized = true;
    return ESP_OK;
}

esp_err_t oclock_todo_create(const char *title,
                             const char *due_date,
                             const char *priority_text)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;

    ESP_RETURN_ON_FALSE(title != NULL && title[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "title required");
    ESP_RETURN_ON_FALSE(parse_due_date(due_date, &year, &month, &day),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid due date");
    ESP_RETURN_ON_FALSE(g_todo.store.count < OCLOCK_TODO_MAX_ITEMS,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "todo capacity reached");

    oclock_todo_item_t *item = &g_todo.store.items[g_todo.store.count++];
    memset(item, 0, sizeof(*item));
    item->id = g_todo.store.next_id++;
    strncpy(item->title, title, sizeof(item->title) - 1);
    item->due_year = year;
    item->due_month = month;
    item->due_day = day;
    item->priority = parse_priority(priority_text);
    item->completed = false;

    return persist_store();
}

esp_err_t oclock_todo_update(uint32_t id,
                             const char *title,
                             const char *due_date,
                             const char *priority_text)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;

    ESP_RETURN_ON_FALSE(title != NULL && title[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "title required");
    ESP_RETURN_ON_FALSE(parse_due_date(due_date, &year, &month, &day),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid due date");

    oclock_todo_item_t *item = find_item(id);
    ESP_RETURN_ON_FALSE(item != NULL, ESP_ERR_NOT_FOUND, TAG, "todo not found");

    memset(item->title, 0, sizeof(item->title));
    strncpy(item->title, title, sizeof(item->title) - 1);
    item->due_year = year;
    item->due_month = month;
    item->due_day = day;
    item->priority = parse_priority(priority_text);

    return persist_store();
}

esp_err_t oclock_todo_set_completed(uint32_t id, bool completed)
{
    oclock_todo_item_t *item = find_item(id);
    ESP_RETURN_ON_FALSE(item != NULL, ESP_ERR_NOT_FOUND, TAG, "todo not found");
    item->completed = completed;
    return persist_store();
}

esp_err_t oclock_todo_delete(uint32_t id)
{
    for (uint32_t i = 0; i < g_todo.store.count; ++i) {
        if (g_todo.store.items[i].id == id) {
            for (uint32_t j = i + 1; j < g_todo.store.count; ++j) {
                g_todo.store.items[j - 1] = g_todo.store.items[j];
            }
            g_todo.store.count--;
            memset(&g_todo.store.items[g_todo.store.count], 0, sizeof(g_todo.store.items[0]));
            return persist_store();
        }
    }
    return ESP_ERR_NOT_FOUND;
}

size_t oclock_todo_list_all(oclock_todo_item_t *items, size_t max_items)
{
    oclock_todo_item_t copy[OCLOCK_TODO_MAX_ITEMS];
    size_t count = g_todo.store.count;
    if (count > OCLOCK_TODO_MAX_ITEMS) {
        count = OCLOCK_TODO_MAX_ITEMS;
    }

    memcpy(copy, g_todo.store.items, count * sizeof(copy[0]));
    qsort(copy, count, sizeof(copy[0]), compare_items_for_display);

    if (items != NULL && max_items > 0) {
        if (count > max_items) {
            count = max_items;
        }
        memcpy(items, copy, count * sizeof(copy[0]));
    }
    return count;
}

bool oclock_todo_is_overdue(const oclock_todo_item_t *item,
                            const oclock_datetime_t *current_time)
{
    if (item == NULL || current_time == NULL || !current_time->valid || item->completed) {
        return false;
    }

    if (item->due_year != current_time->year) {
        return item->due_year < current_time->year;
    }
    if (item->due_month != current_time->month) {
        return item->due_month < current_time->month;
    }
    return item->due_day < current_time->day;
}

const char *oclock_todo_priority_label(uint8_t priority)
{
    switch (priority) {
    case OCLOCK_TODO_PRIORITY_HIGH:
        return "high";
    case OCLOCK_TODO_PRIORITY_LOW:
        return "low";
    case OCLOCK_TODO_PRIORITY_MEDIUM:
    default:
        return "medium";
    }
}

void oclock_todo_build_view(const oclock_datetime_t *current_time,
                            oclock_todo_view_t *view)
{
    oclock_todo_item_t items[OCLOCK_TODO_MAX_ITEMS];
    size_t total = oclock_todo_list_all(items, OCLOCK_TODO_MAX_ITEMS);

    memset(view, 0, sizeof(*view));
    view->total_items = (uint32_t)total;

    for (size_t i = 0; i < total; ++i) {
        if (items[i].completed) {
            view->completed_items++;
            continue;
        }

        if (view->count < OCLOCK_TODO_DISPLAY_MAX) {
            oclock_todo_display_entry_t *entry = &view->entries[view->count++];
            strncpy(entry->title, items[i].title, sizeof(entry->title) - 1);
            snprintf(entry->due_text,
                     sizeof(entry->due_text),
                     "%04u-%02u-%02u",
                     items[i].due_year,
                     items[i].due_month,
                     items[i].due_day);
            strncpy(entry->priority_text,
                    oclock_todo_priority_label(items[i].priority),
                    sizeof(entry->priority_text) - 1);
            entry->overdue = oclock_todo_is_overdue(&items[i], current_time);
        }
        view->open_items++;
    }
}

uint32_t oclock_todo_get_version(void)
{
    return g_todo.version;
}
