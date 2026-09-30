#ifndef OCLOCK_AUDIO_H
#define OCLOCK_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OCLOCK_AUDIO_IDLE,
    OCLOCK_AUDIO_PREPARING,
    OCLOCK_AUDIO_RECORDING,
    OCLOCK_AUDIO_PLAYING,
    OCLOCK_AUDIO_DONE,
    OCLOCK_AUDIO_CANCELLED,
    OCLOCK_AUDIO_ERROR,
} oclock_audio_state_t;

typedef struct {
    oclock_audio_state_t state;
    bool ready;
    bool busy;
    uint32_t version;
    uint32_t elapsed_ms;
    esp_err_t error;
} oclock_audio_status_t;

/**
 * Initialize the board audio codec and I2S channels.
 *
 * Audio is optional during the first rollout: a codec error is logged and
 * reported to the caller without making the clock application fail to boot.
 */
esp_err_t oclock_audio_init(void);

/** Start a short record-then-playback hardware self-test in a worker task. */
esp_err_t oclock_audio_start_self_test(void);
void oclock_audio_cancel(void);
void oclock_audio_get_status(oclock_audio_status_t *out_status);

/** Return whether the codec and I2S data path initialized successfully. */
bool oclock_audio_is_ready(void);

/** Return whether a self-test task is currently recording or playing. */
bool oclock_audio_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif
