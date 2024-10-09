#ifndef __RB_LOG_H__
#define __RB_LOG_H__

#include "stdio.h"

// log svr debug enable
#define RB_LOG_DEBUG_ENABLE  0
// log to buffer enable
#define RB_LOG_BUFF_ENABLE   1
// log buffer size
#define RB_LOG_BUFF_SIZE     (5 * 1024)
// log to uart enable
#define RB_LOG_UART_ENABLE   1

#if __cplusplus
extern "C" {
#endif

int rb_log_init(void);
int rb_log_get_msg_num(void);
char* rb_log_get_msg(void);
int rb_log_free_msg(char* msg);

#if __cplusplus
}
#endif
#endif // !__RB_LOG_H__
