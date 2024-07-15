#ifndef __WS_AUDIO_H__
#define __WS_AUDIO_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

void ws_audio_start(char* url);
void ws_audio_stop(void);
bool ws_audio_is_connected(void);
void ws_audio_send_str(char* str);
void ws_audio_send_buff(uint8_t* buff, int len);


#ifdef __cplusplus
}
#endif
#endif // !__WS_AUDIO_H__
