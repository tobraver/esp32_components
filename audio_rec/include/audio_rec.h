#ifndef __AUDIO_REC_H__
#define __AUDIO_REC_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

/**
 * @brief audio recorder player default volume
 */
#define AUDIO_REC_PLAYER_DEF_VOLUME 70

/**
 * @brief audio recorder wakeup timeout [ms]
 * 
 * @note audio enter sleep after wakeup timeout
 */
#define AUDIO_REC_WAKEUP_TIMEOUT    15*10000

/**
 * @brief audio recorder vad check user speak use time [ms]
 */
#define AUDIO_REC_VAD_SPEAK_TIME    160

/**
 * @brief audio recorder vad check user silence use time [ms]
 */
#define AUDIO_REC_VAD_SILENCE_TIME  1000

typedef enum {
    AUDIO_REC_WAKEUP,
    AUDIO_REC_SPEAK_START,
    AUDIO_REC_SPEAKING,
    AUDIO_REC_SPEAK_WORD,
    AUDIO_REC_SPEAK_END,
    AUDIO_REC_SLEEP,
} audio_rec_event_t;

typedef enum {
    AUDIO_REC_PLAYER_TYPE_PCM,
    AUDIO_REC_PLAYER_TYPE_WAV,
    AUDIO_REC_PLAYER_TYPE_MP3,
} audio_rec_player_type_t;

typedef void (*audio_rec_event_cb_t)(audio_rec_event_t event, void* src, int len);

typedef struct {
    char* cmd_word;
    audio_rec_player_type_t player_type;
    audio_rec_event_cb_t event_cb;
} audio_rec_conf_t;

#ifdef __cplusplus
extern "C" {
#endif

bool audio_rec_init(audio_rec_conf_t conf);
bool audio_rec_deinit(void);
bool audio_rec_set_volume(int volume);
bool audio_rec_play(void* src, int len);

#ifdef __cplusplus
}
#endif
#endif // !__AUDIO_REC_H__
