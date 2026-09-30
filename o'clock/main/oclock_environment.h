#ifndef OCLOCK_ENVIRONMENT_H
#define OCLOCK_ENVIRONMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "oclock_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char temperature_text[16];
    char humidity_text[20];
    char sampled_text[32];
    char comfort_text[24];
    bool valid;
} oclock_environment_view_t;

esp_err_t oclock_environment_init(void);
esp_err_t oclock_environment_tick(void);
void oclock_environment_build_view(const oclock_datetime_t *current_time,
                                   oclock_environment_view_t *out_view);
uint32_t oclock_environment_get_version(void);

#ifdef __cplusplus
}
#endif

#endif
