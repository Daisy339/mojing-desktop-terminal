#ifndef OCLOCK_BOARD_CONFIG_H
#define OCLOCK_BOARD_CONFIG_H

#include "driver/gpio.h"

/* ---------------------------------------------------------------------------
 * Machine-specific settings (Wi-Fi credentials, voice-gateway address and
 * token) are NOT stored in this file. They live in `oclock_secrets.h`, which
 * is listed in .gitignore so that real credentials never reach the repository.
 *
 *   cp main/oclock_secrets.h.example main/oclock_secrets.h
 *   # then edit oclock_secrets.h with your own values
 *
 * The block below provides safe fallbacks, so the firmware still compiles on a
 * fresh clone. With an empty SSID the device simply boots straight into AP
 * setup mode, which is the expected first-run experience.
 * ------------------------------------------------------------------------- */
#if defined(__has_include)
#if __has_include("oclock_secrets.h")
#include "oclock_secrets.h"
#endif
#endif

#ifndef OCLOCK_VOICE_GATEWAY_URL
#define OCLOCK_VOICE_GATEWAY_URL "http://192.168.1.100:8765/audio/ask"
#endif

#ifndef OCLOCK_VOICE_GATEWAY_TOKEN
#define OCLOCK_VOICE_GATEWAY_TOKEN ""
#endif

#ifndef OCLOCK_WIFI_SSID
#define OCLOCK_WIFI_SSID ""
#endif

#ifndef OCLOCK_WIFI_PASSWORD
#define OCLOCK_WIFI_PASSWORD ""
#endif

/* ---------------------------------------------------------------------------
 * Display
 * ------------------------------------------------------------------------- */
#define OCLOCK_LCD_WIDTH 400
#define OCLOCK_LCD_HEIGHT 300

/* RLCD (ST7305) SPI bus */
#define OCLOCK_RLCD_DC_PIN GPIO_NUM_5
#define OCLOCK_RLCD_CS_PIN GPIO_NUM_40
#define OCLOCK_RLCD_SCK_PIN GPIO_NUM_11
#define OCLOCK_RLCD_MOSI_PIN GPIO_NUM_12
#define OCLOCK_RLCD_RST_PIN GPIO_NUM_41
#define OCLOCK_RLCD_TE_PIN GPIO_NUM_6

/* ---------------------------------------------------------------------------
 * I2C — SHTC3 temperature/humidity sensor + PCF85063 RTC
 * ------------------------------------------------------------------------- */
#define OCLOCK_I2C_SDA_PIN GPIO_NUM_13
#define OCLOCK_I2C_SCL_PIN GPIO_NUM_14

/* ---------------------------------------------------------------------------
 * Audio — ES8311 codec (playback) + ES7210 ADC (microphone array)
 * ------------------------------------------------------------------------- */
#define OCLOCK_AUDIO_I2S_MCLK_PIN GPIO_NUM_16
#define OCLOCK_AUDIO_I2S_WS_PIN GPIO_NUM_45
#define OCLOCK_AUDIO_I2S_BCLK_PIN GPIO_NUM_9
#define OCLOCK_AUDIO_I2S_DIN_PIN GPIO_NUM_10
#define OCLOCK_AUDIO_I2S_DOUT_PIN GPIO_NUM_8
#define OCLOCK_AUDIO_PA_PIN GPIO_NUM_46

/* ---------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */
#define OCLOCK_NTP_SERVER "pool.ntp.org"
#define OCLOCK_TIMEZONE "CST-8"

/* ---------------------------------------------------------------------------
 * Buttons: KEY = page key, BOOT = voice trigger
 * ------------------------------------------------------------------------- */
#define OCLOCK_BUTTON_PRIMARY_GPIO GPIO_NUM_18
#define OCLOCK_BUTTON_FALLBACK_GPIO GPIO_NUM_0
#define OCLOCK_BUTTON_ACTIVE_LEVEL 0
#define OCLOCK_BUTTON_DEBOUNCE_MS 40
#define OCLOCK_BUTTON_LONG_PRESS_MS 1000

#endif
