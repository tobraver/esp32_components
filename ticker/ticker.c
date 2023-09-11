#include "ticker.h"

static void ticker_internal_callback(void* arg)
{
    ticker_t* ticker = (ticker_t*)arg;
    ticker->callback();
}


void ticker_init(ticker_t* ticker)
{
    ticker->timer = NULL;
    esp_timer_create_args_t conf = {0};
    conf.callback = &ticker_internal_callback,
    conf.arg = ticker,
	esp_timer_create(&conf, &ticker->timer);
}

void ticker_exec(ticker_t* ticker, bool repeat)
{
    if((ticker->callback == NULL) || (ticker->timeout == 0) || (ticker->timer == NULL)) 
    {
        return;
    }
    
    esp_timer_stop(ticker->timer);

    if(repeat)
    {
        esp_timer_start_periodic(ticker->timer, ticker->timeout*1000);
    }
    else 
    {
        esp_timer_start_once(ticker->timer, ticker->timeout*1000);
    }
}

void ticker_once(ticker_t* ticker)
{
    ticker_exec(ticker, false);
}

void ticker_attach(ticker_t* ticker)
{
    ticker_exec(ticker, true);
}

void ticker_detach(ticker_t* ticker)
{
    if(ticker->timer)
    {
        esp_timer_stop(ticker->timer);
        esp_timer_delete(ticker->timer);
        ticker->timer = NULL;
    }
}

bool ticker_active(ticker_t* ticker)
{
    if(ticker->timer == NULL) return false;
    return esp_timer_is_active(ticker->timer);
}





