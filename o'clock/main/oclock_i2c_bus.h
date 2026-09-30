#ifndef OCLOCK_I2C_BUS_H
#define OCLOCK_I2C_BUS_H

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oclock_i2c_bus_init(void);
i2c_master_bus_handle_t oclock_i2c_bus_handle(void);
esp_err_t oclock_i2c_bus_add_device(uint8_t address,
                                    uint32_t speed_hz,
                                    i2c_master_dev_handle_t *out_device);
esp_err_t oclock_i2c_write_register(i2c_master_dev_handle_t device,
                                    int reg,
                                    const uint8_t *buf,
                                    uint8_t len);
esp_err_t oclock_i2c_read_register(i2c_master_dev_handle_t device,
                                   int reg,
                                   uint8_t *buf,
                                   uint8_t len);

#ifdef __cplusplus
}
#endif

#endif
