#ifndef __TICKER_H__
#define __TICKER_H__

#include "stdio.h"
#include "stdint.h"

#include "esp_timer.h"

typedef void(*ticker_callback_t)(void);

typedef struct
{
    uint32_t           timeout;  // timeout [ms]
    ticker_callback_t  callback; // callback
    esp_timer_handle_t timer;    // timer
} ticker_t;

void ticker_init(ticker_t* ticker);
void ticker_once(ticker_t* ticker);
void ticker_attach(ticker_t* ticker);
void ticker_detach(ticker_t* ticker);
bool ticker_active(ticker_t* ticker);

#endif // !__TICKER_H__
