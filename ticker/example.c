#include "ticker.h"

void ticker_callback(void)
{
    printf("Hello world\n");
}

void example_ticker(void)
{
    ticker_t ticker1;
    ticker1.callback = ticker_callback;
    ticker1.timeout = 100;

    ticker_init(&ticker1);

    while (1)
    {
        ticker_once(&ticker1);

        // delay_ms(50);
        delay_ms(200);
    }
} 


