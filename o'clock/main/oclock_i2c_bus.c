#include "oclock_i2c_bus.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "oclock_board_config.h"

static i2c_master_bus_handle_t g_i2c_bus = NULL;

esp_err_t oclock_i2c_bus_init(void)
{
    if (g_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t config = {0};
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.i2c_port = I2C_NUM_0;
    config.scl_io_num = OCLOCK_I2C_SCL_PIN;
    config.sda_io_num = OCLOCK_I2C_SDA_PIN;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&config, &g_i2c_bus);
}

i2c_master_bus_handle_t oclock_i2c_bus_handle(void)
{
    return g_i2c_bus;
}

esp_err_t oclock_i2c_bus_add_device(uint8_t address,
                                    uint32_t speed_hz,
                                    i2c_master_dev_handle_t *out_device)
{
    ESP_RETURN_ON_FALSE(out_device != NULL, ESP_ERR_INVALID_ARG, "oclock_i2c_bus", "out_device is null");
    ESP_RETURN_ON_ERROR(oclock_i2c_bus_init(), "oclock_i2c_bus", "i2c init failed");

    i2c_device_config_t dev_cfg = {0};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = speed_hz;
    dev_cfg.device_address = address;
    return i2c_master_bus_add_device(g_i2c_bus, &dev_cfg, out_device);
}

esp_err_t oclock_i2c_write_register(i2c_master_dev_handle_t device,
                                    int reg,
                                    const uint8_t *buf,
                                    uint8_t len)
{
    ESP_RETURN_ON_FALSE(g_i2c_bus != NULL, ESP_ERR_INVALID_STATE, "oclock_i2c_bus", "bus not ready");
    const TickType_t data_ticks = pdMS_TO_TICKS(5000);
    const TickType_t done_ticks = pdMS_TO_TICKS(1000);

    esp_err_t ret = i2c_master_bus_wait_all_done(g_i2c_bus, done_ticks);
    if (ret != ESP_OK) {
        return ret;
    }

    if (reg == -1) {
        return i2c_master_transmit(device, buf, len, data_ticks);
    }

    uint8_t stack_buf[32];
    if ((len + 1U) > sizeof(stack_buf)) {
        return ESP_ERR_INVALID_SIZE;
    }

    stack_buf[0] = (uint8_t)reg;
    for (uint8_t i = 0; i < len; ++i) {
        stack_buf[i + 1U] = buf[i];
    }
    return i2c_master_transmit(device, stack_buf, len + 1U, data_ticks);
}

esp_err_t oclock_i2c_read_register(i2c_master_dev_handle_t device,
                                   int reg,
                                   uint8_t *buf,
                                   uint8_t len)
{
    ESP_RETURN_ON_FALSE(g_i2c_bus != NULL, ESP_ERR_INVALID_STATE, "oclock_i2c_bus", "bus not ready");
    const TickType_t data_ticks = pdMS_TO_TICKS(5000);
    const TickType_t done_ticks = pdMS_TO_TICKS(1000);

    esp_err_t ret = i2c_master_bus_wait_all_done(g_i2c_bus, done_ticks);
    if (ret != ESP_OK) {
        return ret;
    }

    if (reg == -1) {
        return i2c_master_receive(device, buf, len, data_ticks);
    }

    uint8_t addr = (uint8_t)reg;
    return i2c_master_transmit_receive(device, &addr, 1, buf, len, data_ticks);
}
