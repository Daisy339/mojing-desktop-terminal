#include "oclock_rlcd_port.h"

#include <assert.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "oclock_board_config.h"

namespace {

constexpr uint8_t kColorBlack = 0x00;
constexpr uint8_t kColorWhite = 0xFF;

class OclockDisplayPort {
public:
    esp_err_t init()
    {
        if (initialized_) {
            return ESP_OK;
        }

        spi_bus_config_t buscfg = {};
        buscfg.miso_io_num = -1;
        buscfg.mosi_io_num = OCLOCK_RLCD_MOSI_PIN;
        buscfg.sclk_io_num = OCLOCK_RLCD_SCK_PIN;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        buscfg.max_transfer_sz = kTransferPixels;

        esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.dc_gpio_num = OCLOCK_RLCD_DC_PIN;
        io_config.cs_gpio_num = OCLOCK_RLCD_CS_PIN;
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.spi_mode = 0;
        io_config.trans_queue_depth = 10;

        ESP_RETURN_ON_ERROR(
            esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST,
                                     &io_config,
                                     &io_handle_),
            TAG,
            "panel io init failed");

        gpio_config_t gpio_conf = {};
        gpio_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_conf.mode = GPIO_MODE_OUTPUT;
        gpio_conf.pin_bit_mask = (1ULL << OCLOCK_RLCD_RST_PIN);
        gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_RETURN_ON_ERROR(gpio_config(&gpio_conf), TAG, "reset gpio init failed");

        set_reset_level(1);

        display_len_ = kTransferPixels >> 3;
        buffer_ = static_cast<uint8_t *>(
            heap_caps_malloc(display_len_, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (buffer_ == nullptr) {
            buffer_ = static_cast<uint8_t *>(
                heap_caps_malloc(display_len_, MALLOC_CAP_8BIT));
        }
        ESP_RETURN_ON_FALSE(buffer_ != nullptr, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");

        pixel_index_lut_ = static_cast<uint16_t (*)[OCLOCK_LCD_HEIGHT]>(
            heap_caps_malloc(kTransferPixels * sizeof(uint16_t),
                             MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (pixel_index_lut_ == nullptr) {
            pixel_index_lut_ = static_cast<uint16_t (*)[OCLOCK_LCD_HEIGHT]>(
                heap_caps_malloc(kTransferPixels * sizeof(uint16_t), MALLOC_CAP_8BIT));
        }
        ESP_RETURN_ON_FALSE(pixel_index_lut_ != nullptr,
                            ESP_ERR_NO_MEM,
                            TAG,
                            "pixel index LUT alloc failed");

        pixel_bit_lut_ = static_cast<uint8_t (*)[OCLOCK_LCD_HEIGHT]>(
            heap_caps_malloc(kTransferPixels * sizeof(uint8_t),
                             MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (pixel_bit_lut_ == nullptr) {
            pixel_bit_lut_ = static_cast<uint8_t (*)[OCLOCK_LCD_HEIGHT]>(
                heap_caps_malloc(kTransferPixels * sizeof(uint8_t), MALLOC_CAP_8BIT));
        }
        ESP_RETURN_ON_FALSE(pixel_bit_lut_ != nullptr,
                            ESP_ERR_NO_MEM,
                            TAG,
                            "pixel bit LUT alloc failed");

        init_landscape_lut();
        rlcd_init();
        initialized_ = true;
        return ESP_OK;
    }

    void clear(uint8_t color)
    {
        if (buffer_ != nullptr) {
            memset(buffer_, color, display_len_);
        }
    }

    void set_pixel(uint16_t x, uint16_t y, uint8_t color)
    {
        if (x >= OCLOCK_LCD_WIDTH || y >= OCLOCK_LCD_HEIGHT || buffer_ == nullptr) {
            return;
        }

        const uint32_t idx = pixel_index_lut_[x][y];
        const uint8_t mask = pixel_bit_lut_[x][y];
        uint8_t *p = &buffer_[idx];

        if (color != 0) {
            *p |= mask;
        } else {
            *p &= static_cast<uint8_t>(~mask);
        }
    }

    esp_err_t present()
    {
        ESP_RETURN_ON_FALSE(io_handle_ != nullptr, ESP_ERR_INVALID_STATE, TAG, "not initialized");

        static const uint8_t win_a[] = {0x12, 0x2A};
        static const uint8_t win_b[] = {0x00, 0xC7};

        ESP_RETURN_ON_ERROR(rlcd_send_command_data(0x2A, win_a, sizeof(win_a)),
                            TAG,
                            "column set failed");
        ESP_RETURN_ON_ERROR(rlcd_send_command_data(0x2B, win_b, sizeof(win_b)),
                            TAG,
                            "row set failed");
        ESP_RETURN_ON_ERROR(rlcd_send_command(0x2C), TAG, "memory write cmd failed");
        ESP_RETURN_ON_ERROR(rlcd_send_buffer(buffer_, display_len_),
                            TAG,
                            "frame transfer failed");
        return ESP_OK;
    }

private:
    static constexpr const char *TAG = "oclock_rlcd";
    static constexpr int kTransferPixels = OCLOCK_LCD_WIDTH * OCLOCK_LCD_HEIGHT;

    esp_lcd_panel_io_handle_t io_handle_ = nullptr;
    uint8_t *buffer_ = nullptr;
    uint16_t (*pixel_index_lut_)[OCLOCK_LCD_HEIGHT] = nullptr;
    uint8_t (*pixel_bit_lut_)[OCLOCK_LCD_HEIGHT] = nullptr;
    int display_len_ = 0;
    bool initialized_ = false;

    void init_landscape_lut()
    {
        const uint16_t h4 = OCLOCK_LCD_HEIGHT >> 2;

        for (uint16_t y = 0; y < OCLOCK_LCD_HEIGHT; ++y) {
            const uint16_t inv_y = static_cast<uint16_t>(OCLOCK_LCD_HEIGHT - 1 - y);
            const uint16_t block_y = inv_y >> 2;
            const uint8_t local_y = static_cast<uint8_t>(inv_y & 3U);

            for (uint16_t x = 0; x < OCLOCK_LCD_WIDTH; ++x) {
                const uint16_t byte_x = x >> 1;
                const uint8_t local_x = static_cast<uint8_t>(x & 1U);
                const uint32_t index = static_cast<uint32_t>(byte_x) * h4 + block_y;
                const uint8_t bit =
                    static_cast<uint8_t>(7U - ((local_y << 1U) | local_x));

                pixel_index_lut_[x][y] = static_cast<uint16_t>(index);
                pixel_bit_lut_[x][y] = static_cast<uint8_t>(1U << bit);
            }
        }
    }

    void set_reset_level(uint8_t level)
    {
        gpio_set_level(OCLOCK_RLCD_RST_PIN, level ? 1 : 0);
    }

    void rlcd_reset()
    {
        set_reset_level(1);
        vTaskDelay(pdMS_TO_TICKS(50));
        set_reset_level(0);
        vTaskDelay(pdMS_TO_TICKS(20));
        set_reset_level(1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_err_t rlcd_send_command(uint8_t reg)
    {
        return esp_lcd_panel_io_tx_param(io_handle_, reg, nullptr, 0);
    }

    esp_err_t rlcd_send_command_data(uint8_t reg, const uint8_t *data, size_t len)
    {
        return esp_lcd_panel_io_tx_param(io_handle_, reg, data, len);
    }

    esp_err_t rlcd_send_buffer(uint8_t *data, int len)
    {
        return esp_lcd_panel_io_tx_color(io_handle_, -1, data, len);
    }

    void rlcd_init()
    {
        rlcd_reset();

        const uint8_t d6[] = {0x17, 0x02};
        const uint8_t d1[] = {0x01};
        const uint8_t c0[] = {0x11, 0x04};
        const uint8_t c1[] = {0x69, 0x69, 0x69, 0x69};
        const uint8_t c2[] = {0x19, 0x19, 0x19, 0x19};
        const uint8_t c4[] = {0x4B, 0x4B, 0x4B, 0x4B};
        const uint8_t d8[] = {0x80, 0xE9};
        const uint8_t b2[] = {0x02};
        const uint8_t b3[] = {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
        const uint8_t b4[] = {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
        const uint8_t g_timing[] = {0x32, 0x03, 0x1F};
        const uint8_t b7[] = {0x13};
        const uint8_t b0[] = {0x64};
        const uint8_t c9[] = {0x00};
        const uint8_t m36[] = {0x48};
        const uint8_t m3a[] = {0x11};
        const uint8_t b9[] = {0x20};
        const uint8_t b8[] = {0x29};
        const uint8_t win_a[] = {0x12, 0x2A};
        const uint8_t win_b[] = {0x00, 0xC7};
        const uint8_t m35[] = {0x00};
        const uint8_t d0[] = {0xFF};

        ESP_ERROR_CHECK(rlcd_send_command_data(0xD6, d6, sizeof(d6)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xD1, d1, sizeof(d1)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xC0, c0, sizeof(c0)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xC1, c1, sizeof(c1)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xC2, c2, sizeof(c2)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xC4, c4, sizeof(c4)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xC5, c2, sizeof(c2)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xD8, d8, sizeof(d8)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xB2, b2, sizeof(b2)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xB3, b3, sizeof(b3)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xB4, b4, sizeof(b4)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0x62, g_timing, sizeof(g_timing)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xB7, b7, sizeof(b7)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xB0, b0, sizeof(b0)));
        ESP_ERROR_CHECK(rlcd_send_command(0x11));
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xC9, c9, sizeof(c9)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0x36, m36, sizeof(m36)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0x3A, m3a, sizeof(m3a)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xB9, b9, sizeof(b9)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xB8, b8, sizeof(b8)));
        ESP_ERROR_CHECK(rlcd_send_command(0x21));
        ESP_ERROR_CHECK(rlcd_send_command_data(0x2A, win_a, sizeof(win_a)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0x2B, win_b, sizeof(win_b)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0x35, m35, sizeof(m35)));
        ESP_ERROR_CHECK(rlcd_send_command_data(0xD0, d0, sizeof(d0)));
        ESP_ERROR_CHECK(rlcd_send_command(0x38));
        ESP_ERROR_CHECK(rlcd_send_command(0x29));

        clear(kColorWhite);
        ESP_ERROR_CHECK(present());
    }
};

OclockDisplayPort g_display;

} // namespace

extern "C" esp_err_t oclock_rlcd_port_init(void)
{
    return g_display.init();
}

extern "C" void oclock_rlcd_port_clear(bool draw_black)
{
    g_display.clear(draw_black ? kColorBlack : kColorWhite);
}

extern "C" void oclock_rlcd_port_set_pixel(uint16_t x, uint16_t y, bool draw_black)
{
    g_display.set_pixel(x, y, draw_black ? kColorBlack : kColorWhite);
}

extern "C" esp_err_t oclock_rlcd_port_present(void)
{
    return g_display.present();
}
