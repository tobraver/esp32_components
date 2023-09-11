#ifndef __MUTEX_H__
#define __MUTEX_H__

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef SemaphoreHandle_t mutex_t;

mutex_t mutex_create(void);
void mutex_lock(mutex_t mutex);
void mutex_unlock(mutex_t mutex);


#endif // !__MUTEX_H__

