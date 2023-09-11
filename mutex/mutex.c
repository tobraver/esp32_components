#include "mutex.h"

mutex_t mutex_create(void)
{
    return xSemaphoreCreateMutex();
}

void mutex_lock(mutex_t mutex)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
}

void mutex_unlock(mutex_t mutex)
{
    xSemaphoreGive(mutex);
}
