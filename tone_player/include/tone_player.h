#ifndef __TONE_PLAYER_H__
#define __TONE_PLAYER_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"
#include "audio_tone_uri.h"

// tone player default volume
#define TONE_PLAYER_DEF_VOLUME 80

#if __cplusplus
extern "C" {
#endif

bool tone_player_init(void);
bool tone_player_deinit(void);
bool tone_player_sync_play(tone_type_t type);
bool tone_player_async_play(tone_type_t type);
bool tone_player_stop(bool is_wait);
bool tone_player_is_playing(void);
bool tone_player_set_volume(int32_t volume);

#if __cplusplus
}
#endif
#endif // !__TONE_PLAYER_H__
