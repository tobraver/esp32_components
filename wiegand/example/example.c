#include "wiegand.h"

void example(void)
{
    wiegand_t hwiegand = {0};
    hwiegand.d0_io_num = GPIO_NUM_6;
    hwiegand.d1_io_num = GPIO_NUM_7;
    wiegand_init(&hwiegand);

    uint32_t data = 0x0FF00FF0;

    while (1)
    {
        wiegand34_send(&hwiegand, data);
        delay_ms(1000);
    }
} 