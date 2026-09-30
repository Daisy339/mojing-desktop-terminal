#ifndef OCLOCK_RLCD_PORT_H
#define OCLOCK_RLCD_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oclock_rlcd_port_init(void);
void oclock_rlcd_port_clear(bool draw_black);
void oclock_rlcd_port_set_pixel(uint16_t x, uint16_t y, bool draw_black);
esp_err_t oclock_rlcd_port_present(void);

#ifdef __cplusplus
}
#endif

#endif
