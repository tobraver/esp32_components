#ifndef __RB_LOG_H__
#define __RB_LOG_H__

#include "stdio.h"

// log debug enable
#define RB_LOG_DEBUG_ENABLE  0
// log to buffer enable
#define RB_LOG_BUFF_ENABLE   1
// log buffer use psram
#define RB_LOG_BUFF_PSRAM    1
// log buffer size
#define RB_LOG_BUFF_SIZE     (500 * 1024)

#if __cplusplus
extern "C" {
#endif

int rb_log_init(void);
int rb_log_get_free_size(void);
int rb_log_get_msg_num(void);
char* rb_log_get_msg(void);
int rb_log_free_msg(char* msg);

#if __cplusplus
}
#endif
#endif // !__RB_LOG_H__
