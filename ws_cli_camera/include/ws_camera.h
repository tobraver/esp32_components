#ifndef __WS_CAMERA_H__
#define __WS_CAMERA_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

void ws_camera_start(char* url);
void ws_camera_stop(void);
bool ws_camera_is_connected(void);
void ws_camera_send_str(char* str);
void ws_camera_send_buff(uint8_t* buff, int len);


#ifdef __cplusplus
}
#endif
#endif // !__WS_CAMERA_H__
