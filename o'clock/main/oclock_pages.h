#ifndef OCLOCK_PAGES_H
#define OCLOCK_PAGES_H

#include <stdbool.h>

typedef enum {
    OCLOCK_PAGE_HOME = 0,
    OCLOCK_PAGE_WEATHER,
    OCLOCK_PAGE_TODO,
    OCLOCK_PAGE_ALBUM,
    OCLOCK_PAGE_COUNT
} oclock_page_id_t;

const char *oclock_page_name(oclock_page_id_t page);
const char *oclock_page_placeholder_copy(oclock_page_id_t page);
bool oclock_page_is_auto_rotation_member(oclock_page_id_t page);
oclock_page_id_t oclock_page_next_manual(oclock_page_id_t current_page);
oclock_page_id_t oclock_page_next_auto(oclock_page_id_t current_page);

#endif
