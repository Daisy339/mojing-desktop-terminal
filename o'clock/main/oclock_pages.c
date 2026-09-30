#include "oclock_pages.h"

const char *oclock_page_name(oclock_page_id_t page)
{
    switch (page) {
    case OCLOCK_PAGE_HOME:
        return "Home";
    case OCLOCK_PAGE_WEATHER:
        return "Weather";
    case OCLOCK_PAGE_TODO:
        return "Todo";
    case OCLOCK_PAGE_ALBUM:
        return "Album";
    case OCLOCK_PAGE_COUNT:
    default:
        return "Unknown";
    }
}

const char *oclock_page_placeholder_copy(oclock_page_id_t page)
{
    switch (page) {
    case OCLOCK_PAGE_HOME:
        return "Clock-first home page shell";
    case OCLOCK_PAGE_WEATHER:
        return "Manual-only weather details shell";
    case OCLOCK_PAGE_TODO:
        return "Todo summary page shell";
    case OCLOCK_PAGE_ALBUM:
        return "Album and frame playback shell";
    case OCLOCK_PAGE_COUNT:
    default:
        return "Unknown page shell";
    }
}

bool oclock_page_is_auto_rotation_member(oclock_page_id_t page)
{
    return page == OCLOCK_PAGE_HOME || page == OCLOCK_PAGE_TODO ||
           page == OCLOCK_PAGE_ALBUM;
}

oclock_page_id_t oclock_page_next_manual(oclock_page_id_t current_page)
{
    return (oclock_page_id_t)((current_page + 1) % OCLOCK_PAGE_COUNT);
}

oclock_page_id_t oclock_page_next_auto(oclock_page_id_t current_page)
{
    switch (current_page) {
    case OCLOCK_PAGE_HOME:
        return OCLOCK_PAGE_TODO;
    case OCLOCK_PAGE_TODO:
        return OCLOCK_PAGE_ALBUM;
    case OCLOCK_PAGE_ALBUM:
    case OCLOCK_PAGE_WEATHER:
    case OCLOCK_PAGE_COUNT:
    default:
        return OCLOCK_PAGE_HOME;
    }
}
