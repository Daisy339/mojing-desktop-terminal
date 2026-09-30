#include "oclock_audio.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "oclock_board_config.h"
#include "oclock_i2c_bus.h"

namespace {

static const char *TAG = "oclock_audio";

constexpr int kSampleRate = 24000;
constexpr int kBitsPerSample = 16;
constexpr int kRecordChannels = 4;
constexpr int kPlaybackChannels = 1;
constexpr int kRecordSeconds = 5;
constexpr int kPlaybackVolume = 100; // Codec volume scale: 0-100.
constexpr int kPcmBytesPerSecond =
    kSampleRate * kPlaybackChannels * (kBitsPerSample / 8);
constexpr size_t kRecordBytes =
    static_cast<size_t>(kPcmBytesPerSecond) * kRecordSeconds;
constexpr size_t kMaxGatewayWavBytes = 3 * 1024 * 1024;

struct AudioContext {
    i2s_chan_handle_t tx_handle = nullptr;
    i2s_chan_handle_t rx_handle = nullptr;
    esp_codec_dev_handle_t output = nullptr;
    esp_codec_dev_handle_t input = nullptr;
    const audio_codec_data_if_t *data_if = nullptr;
    const audio_codec_gpio_if_t *gpio_if = nullptr;
    const audio_codec_ctrl_if_t *output_ctrl = nullptr;
    const audio_codec_ctrl_if_t *input_ctrl = nullptr;
    const audio_codec_if_t *output_codec = nullptr;
    const audio_codec_if_t *input_codec = nullptr;
    bool initialized = false;
    TaskHandle_t task = nullptr;
};

AudioContext g_audio;
portMUX_TYPE g_status_lock = portMUX_INITIALIZER_UNLOCKED;
oclock_audio_status_t g_status = {};
bool g_cancel = false;

void publish(oclock_audio_state_t state, uint32_t elapsed_ms = 0,
             esp_err_t error = ESP_OK)
{
    portENTER_CRITICAL(&g_status_lock);
    g_status.state = state;
    g_status.elapsed_ms = elapsed_ms;
    g_status.error = error;
    g_status.busy = state == OCLOCK_AUDIO_PREPARING ||
                    state == OCLOCK_AUDIO_RECORDING || state == OCLOCK_AUDIO_PLAYING;
    ++g_status.version;
    portEXIT_CRITICAL(&g_status_lock);
}

bool cancelled()
{
    portENTER_CRITICAL(&g_status_lock);
    bool value = g_cancel;
    portEXIT_CRITICAL(&g_status_lock);
    return value;
}

void destroy_devices()
{
    if (g_audio.input) {
        esp_codec_dev_close(g_audio.input);
        esp_codec_dev_delete(g_audio.input);
    }
    if (g_audio.output) {
        esp_codec_dev_close(g_audio.output);
        esp_codec_dev_delete(g_audio.output);
    }
    if (g_audio.input_codec) audio_codec_delete_codec_if(g_audio.input_codec);
    if (g_audio.output_codec) audio_codec_delete_codec_if(g_audio.output_codec);
    if (g_audio.input_ctrl) audio_codec_delete_ctrl_if(g_audio.input_ctrl);
    if (g_audio.output_ctrl) audio_codec_delete_ctrl_if(g_audio.output_ctrl);
    if (g_audio.gpio_if) audio_codec_delete_gpio_if(g_audio.gpio_if);
    if (g_audio.data_if) audio_codec_delete_data_if(g_audio.data_if);
    if (g_audio.rx_handle) {
        (void)i2s_channel_disable(g_audio.rx_handle);
        i2s_del_channel(g_audio.rx_handle);
    }
    if (g_audio.tx_handle) {
        (void)i2s_channel_disable(g_audio.tx_handle);
        i2s_del_channel(g_audio.tx_handle);
    }
    gpio_set_level(OCLOCK_AUDIO_PA_PIN, 0);
    g_audio = {};
}

esp_err_t create_i2s_channels()
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = 240;
    channel_config.auto_clear_after_cb = true;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config,
                                        &g_audio.tx_handle,
                                        &g_audio.rx_handle),
                        TAG,
                        "I2S channel creation failed");

    i2s_tdm_config_t tx_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(kSampleRate),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            static_cast<i2s_tdm_slot_mask_t>(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                             I2S_TDM_SLOT2 | I2S_TDM_SLOT3)),
        .gpio_cfg = {
            .mclk = OCLOCK_AUDIO_I2S_MCLK_PIN,
            .bclk = OCLOCK_AUDIO_I2S_BCLK_PIN,
            .ws = OCLOCK_AUDIO_I2S_WS_PIN,
            .dout = OCLOCK_AUDIO_I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    i2s_tdm_config_t rx_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(kSampleRate),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            static_cast<i2s_tdm_slot_mask_t>(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                            I2S_TDM_SLOT2 | I2S_TDM_SLOT3)),
        .gpio_cfg = {
            .mclk = OCLOCK_AUDIO_I2S_MCLK_PIN,
            .bclk = OCLOCK_AUDIO_I2S_BCLK_PIN,
            .ws = OCLOCK_AUDIO_I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = OCLOCK_AUDIO_I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(g_audio.tx_handle,
                                                  &tx_config),
                        TAG,
                        "I2S TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(g_audio.rx_handle,
                                                  &rx_config),
                        TAG,
                        "I2S RX init failed");
    return ESP_OK;
}

esp_err_t create_codec_devices()
{
    audio_codec_i2s_cfg_t i2s_config = {
        .port = I2S_NUM_0,
        .rx_handle = g_audio.rx_handle,
        .tx_handle = g_audio.tx_handle,
    };
    g_audio.data_if = audio_codec_new_i2s_data(&i2s_config);
    ESP_RETURN_ON_FALSE(g_audio.data_if != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "I2S codec data interface failed");

    audio_codec_i2c_cfg_t i2c_config = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = oclock_i2c_bus_handle(),
    };
    g_audio.output_ctrl = audio_codec_new_i2c_ctrl(&i2c_config);
    ESP_RETURN_ON_FALSE(g_audio.output_ctrl != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "ES8311 control interface failed");

    g_audio.gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(g_audio.gpio_if != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "codec GPIO interface failed");

    es8311_codec_cfg_t output_config = {};
    output_config.ctrl_if = g_audio.output_ctrl;
    output_config.gpio_if = g_audio.gpio_if;
    output_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    output_config.pa_pin = OCLOCK_AUDIO_PA_PIN;
    output_config.use_mclk = true;
    output_config.hw_gain.pa_voltage = 5.0;
    output_config.hw_gain.codec_dac_voltage = 3.3;
    g_audio.output_codec = es8311_codec_new(&output_config);
    ESP_RETURN_ON_FALSE(g_audio.output_codec != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "ES8311 codec init failed");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = g_audio.output_codec,
        .data_if = g_audio.data_if,
    };
    g_audio.output = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(g_audio.output != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "audio output device failed");

    i2c_config.addr = ES7210_CODEC_DEFAULT_ADDR;
    g_audio.input_ctrl = audio_codec_new_i2c_ctrl(&i2c_config);
    ESP_RETURN_ON_FALSE(g_audio.input_ctrl != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "ES7210 control interface failed");

    es7210_codec_cfg_t input_config = {};
    input_config.ctrl_if = g_audio.input_ctrl;
    input_config.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 |
                                 ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    g_audio.input_codec = es7210_codec_new(&input_config);
    ESP_RETURN_ON_FALSE(g_audio.input_codec != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "ES7210 codec init failed");

    device_config.dev_type = ESP_CODEC_DEV_TYPE_IN;
    device_config.codec_if = g_audio.input_codec;
    g_audio.input = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(g_audio.input != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "audio input device failed");
    return ESP_OK;
}

struct GatewayBuffer {
    uint8_t *data;
    size_t length;
    size_t capacity;
    bool overflow;
    bool allocation_failed;
};

static esp_err_t gateway_event(esp_http_client_event_t *event)
{
    auto *buffer = static_cast<GatewayBuffer *>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_DATA || !buffer || event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t needed = buffer->length + event->data_len;
    if (needed > kMaxGatewayWavBytes) {
        buffer->overflow = true;
        return ESP_OK;
    }
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 65536;
        while (capacity < needed) capacity *= 2;
        capacity = std::min(capacity, kMaxGatewayWavBytes);
        uint8_t *next = static_cast<uint8_t *>(
            heap_caps_realloc(buffer->data, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!next) {
            buffer->allocation_failed = true;
            return ESP_OK;
        }
        buffer->data = next;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, event->data, event->data_len);
    buffer->length += event->data_len;
    return ESP_OK;
}

static uint16_t wav_u16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
static uint32_t wav_u32(const uint8_t *p) { return p[0] | (uint32_t(p[1]) << 8) |
    (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

static esp_err_t gateway_play(uint8_t *pcm, size_t pcm_bytes)
{
    GatewayBuffer response = {};
    esp_http_client_config_t config = {};
    config.url = OCLOCK_VOICE_GATEWAY_URL;
    config.method = HTTP_METHOD_POST;
    config.event_handler = gateway_event;
    config.user_data = &response;
    config.timeout_ms = 90000;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(client, ESP_ERR_NO_MEM, TAG, "gateway client init failed");
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "X-OClock-Audio", "raw");
    esp_http_client_set_header(client, "Authorization", "Bearer " OCLOCK_VOICE_GATEWAY_TOKEN);
    esp_err_t err = esp_http_client_set_post_field(client,
                                                   reinterpret_cast<const char *>(pcm),
                                                   pcm_bytes);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    if (err == ESP_OK && status != 200) err = ESP_FAIL;
    esp_http_client_cleanup(client);
    if (response.overflow) err = ESP_ERR_INVALID_SIZE;
    if (response.allocation_failed) err = ESP_ERR_NO_MEM;
    if (err != ESP_OK || response.length < 44 ||
        memcmp(response.data, "RIFF", 4) != 0 ||
        memcmp(response.data + 8, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "Gateway response rejected: status=%d bytes=%u err=%s",
                 status, unsigned(response.length), esp_err_to_name(err));
        free(response.data);
        return err == ESP_OK ? ESP_FAIL : err;
    }
    const uint16_t channels = wav_u16(response.data + 22);
    const uint32_t sample_rate = wav_u32(response.data + 24);
    const uint16_t bits = wav_u16(response.data + 34);
    const uint32_t data_bytes = wav_u32(response.data + 40);
    const size_t data_offset = 44;
    if (wav_u16(response.data + 20) != 1 || channels != 1 || bits != 16 ||
        sample_rate == 0 || data_bytes == 0 || data_bytes % 2 != 0 ||
        size_t(data_bytes) + data_offset != response.length ||
        size_t(wav_u32(response.data + 4)) + 8 != response.length) {
        ESP_LOGW(TAG, "Incomplete gateway WAV: received=%u declared=%u",
                 unsigned(response.length), unsigned(data_bytes + data_offset));
        free(response.data);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "Gateway WAV complete: %u bytes, %.1f seconds",
             unsigned(response.length), double(data_bytes) / (sample_rate * 2));
    esp_codec_dev_sample_info_t output_info = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0,
        .sample_rate = sample_rate, .mclk_multiple = 0,
    };
    err = esp_codec_dev_open(g_audio.output, &output_info);
    if (err == ESP_OK) err = esp_codec_dev_set_out_vol(g_audio.output, kPlaybackVolume);
    if (err == ESP_OK) publish(OCLOCK_AUDIO_PLAYING);
    for (size_t offset = data_offset; err == ESP_OK && offset < response.length && !cancelled(); ) {
        const size_t chunk = std::min<size_t>(4096, response.length - offset);
        err = esp_codec_dev_write(g_audio.output, response.data + offset, chunk);
        offset += chunk;
    }
    if (err == ESP_OK && !cancelled()) {
        uint8_t silence[1024] = {};
        for (int i = 0; i < 10 && err == ESP_OK; ++i) {
            err = esp_codec_dev_write(g_audio.output, silence, sizeof(silence));
        }
    }
    const esp_err_t close_err = esp_codec_dev_close(g_audio.output);
    if (err == ESP_OK) err = close_err;
    free(response.data);
    return err;
}

esp_err_t record_and_play(uint8_t *buffer)
{
    // Four wire slots; DMA selects ONLY microphone slot 0. The returned PCM is
    // mono (48,000 bytes/s), not four interleaved channels. Slot 1 is reference.
    esp_codec_dev_sample_info_t input_info = {
        .bits_per_sample = kBitsPerSample,
        .channel = kRecordChannels,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = kSampleRate,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(g_audio.input, &input_info),
                        TAG, "audio input open failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_channel_gain(
                            g_audio.input, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), 30.0f),
                        TAG, "microphone gain setup failed");

    // Discard startup samples before the five-second recording window.
    constexpr size_t kChunkBytes = 960; // 20 ms mono PCM16 at 24 kHz
    for (int i = 0; i < 5 && !cancelled(); ++i) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_read(g_audio.input, buffer, kChunkBytes),
                            TAG, "input warmup failed");
    }
    publish(OCLOCK_AUDIO_RECORDING);
    ESP_LOGI(TAG, "Recording %d seconds; BOOT cancels", kRecordSeconds);
    int peak = 0;
    size_t captured = 0;
    while (captured < kRecordBytes && !cancelled()) {
        const size_t chunk = std::min(kChunkBytes, kRecordBytes - captured);
        ESP_RETURN_ON_ERROR(esp_codec_dev_read(g_audio.input, buffer + captured, chunk),
                            TAG, "audio capture failed");
        const int16_t *samples = reinterpret_cast<const int16_t *>(buffer + captured);
        for (size_t i = 0; i < chunk / sizeof(int16_t); ++i) {
            peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
        }
        captured += chunk;
        if (captured % kPcmBytesPerSecond == 0) {
            publish(OCLOCK_AUDIO_RECORDING, captured * 1000 / kPcmBytesPerSecond);
        }
    }
    ESP_RETURN_ON_ERROR(esp_codec_dev_close(g_audio.input), TAG, "input close failed");
    if (cancelled()) return ESP_OK;
    ESP_LOGI(TAG, "Captured %u mono bytes, peak=%d/32768", unsigned(captured), peak);

    if (gateway_play(buffer, captured) == ESP_OK) {
        ESP_LOGI(TAG, "Cloud voice response played");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Cloud gateway unavailable; falling back to local recording playback");

    esp_codec_dev_sample_info_t output_info = {
        .bits_per_sample = kBitsPerSample,
        .channel = kPlaybackChannels,
        .channel_mask = 0,
        .sample_rate = kSampleRate,
        .mclk_multiple = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(g_audio.output, &output_info),
                        TAG, "audio output open failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(g_audio.output, kPlaybackVolume),
                        TAG, "speaker volume setup failed");
    publish(OCLOCK_AUDIO_PLAYING);
    ESP_LOGI(TAG, "Playing recorded voice; volume=%d/100; BOOT cancels", kPlaybackVolume);
    size_t played = 0;
    while (played < captured && !cancelled()) {
        const size_t chunk = std::min(kChunkBytes, captured - played);
        ESP_RETURN_ON_ERROR(esp_codec_dev_write(g_audio.output, buffer + played, chunk),
                            TAG, "audio playback failed");
        played += chunk;
        if (played % kPcmBytesPerSecond == 0) {
            publish(OCLOCK_AUDIO_PLAYING, played * 1000 / kPcmBytesPerSecond);
        }
    }
    // Push silence through DMA so that the last spoken samples reach the DAC.
    uint8_t silence[kChunkBytes] = {};
    for (int i = 0; i < 10 && !cancelled(); ++i) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_write(g_audio.output, silence, sizeof(silence)),
                            TAG, "playback drain failed");
    }
    return ESP_OK;
}

void self_test_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        auto *buffer = static_cast<uint8_t *>(
            heap_caps_malloc(kRecordBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        // Avoid draining internal RAM used by Wi-Fi, DMA and task stacks.
        esp_err_t err = buffer ? record_and_play(buffer) : ESP_ERR_NO_MEM;
        const esp_err_t close_in = esp_codec_dev_close(g_audio.input);
        const esp_err_t close_out = esp_codec_dev_close(g_audio.output);
        gpio_set_level(OCLOCK_AUDIO_PA_PIN, 0);
        if (err == ESP_OK) err = close_in != ESP_OK ? close_in : close_out;
        free(buffer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Self-test failed: %s", esp_err_to_name(err));
            publish(OCLOCK_AUDIO_ERROR, 0, err);
        } else {
            bool was_cancelled = cancelled();
            ESP_LOGI(TAG, "Self-test %s", was_cancelled ? "cancelled" : "finished");
            publish(was_cancelled ? OCLOCK_AUDIO_CANCELLED : OCLOCK_AUDIO_DONE);
        }
    }
}

} // namespace

extern "C" esp_err_t oclock_audio_init(void)
{
    if (g_audio.initialized) return ESP_OK;
    esp_err_t err = oclock_i2c_bus_init();
    if (err == ESP_OK) err = create_i2s_channels();
    if (err == ESP_OK) err = create_codec_devices();
    if (err == ESP_OK) {
        // Codec constructors may enable output. Idle always keeps the PA off.
        gpio_config_t pa_config = {
            .pin_bit_mask = 1ULL << OCLOCK_AUDIO_PA_PIN,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&pa_config);
    }
    if (err == ESP_OK) {
        gpio_set_level(OCLOCK_AUDIO_PA_PIN, 0);
        if (xTaskCreate(self_test_task, "oclock_audio", 6144, nullptr, 4,
                        &g_audio.task) != pdPASS) err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        destroy_devices();
        publish(OCLOCK_AUDIO_ERROR, 0, err);
        ESP_LOGW(TAG, "Audio unavailable: %s; clock continues", esp_err_to_name(err));
        return err;
    }
    g_audio.initialized = true;
    portENTER_CRITICAL(&g_status_lock);
    g_status.ready = true;
    portEXIT_CRITICAL(&g_status_lock);
    publish(OCLOCK_AUDIO_IDLE);
    ESP_LOGI(TAG, "Audio initialized at %d Hz; BOOT starts 5-second record/play test", kSampleRate);
    return ESP_OK;
}

extern "C" esp_err_t oclock_audio_start_self_test(void)
{
    portENTER_CRITICAL(&g_status_lock);
    if (!g_status.ready || g_status.busy) {
        portEXIT_CRITICAL(&g_status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    g_cancel = false;
    g_status.state = OCLOCK_AUDIO_PREPARING;
    g_status.elapsed_ms = 0;
    g_status.error = ESP_OK;
    g_status.busy = true;
    ++g_status.version;
    portEXIT_CRITICAL(&g_status_lock);
    xTaskNotifyGive(g_audio.task);
    return ESP_OK;
}

extern "C" void oclock_audio_cancel(void)
{
    portENTER_CRITICAL(&g_status_lock);
    if (g_status.busy) g_cancel = true;
    portEXIT_CRITICAL(&g_status_lock);
}

extern "C" void oclock_audio_get_status(oclock_audio_status_t *out_status)
{
    if (!out_status) return;
    portENTER_CRITICAL(&g_status_lock);
    *out_status = g_status;
    portEXIT_CRITICAL(&g_status_lock);
}

extern "C" bool oclock_audio_is_ready(void)
{
    oclock_audio_status_t status;
    oclock_audio_get_status(&status);
    return status.ready;
}

extern "C" bool oclock_audio_is_busy(void)
{
    oclock_audio_status_t status;
    oclock_audio_get_status(&status);
    return status.busy;
}
