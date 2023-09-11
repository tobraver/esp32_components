#include "bsp_uart.h"

void example_bsp_uart(void)
{
    bsp_uart_conf conf = {0};

    conf.uartx = UART_NUM_1;
    conf.baud_rate = 115200;
    conf.tx_io_num = 15;
    conf.rx_io_num = 16;
    conf.timeout = 1000;
    conf.queue = NULL;
    conf.timer = NULL;

    uint8_t buff[20] = {0};
    bsp_uart_t huart = {0};
    huart.conf = conf;
    huart.buff = buff;
    huart.available = false;
    huart.conf.buf_size = sizeof(buff);

    bsp_uart_init(&huart);

    uint8_t buff2[1024] = {0};
    bsp_uart_t huart1 = huart;
    huart1.conf.uartx = UART_NUM_2;
    huart1.conf.tx_io_num = 18;
    huart1.conf.rx_io_num = 19;
    huart1.buff = buff2;
    huart1.conf.buf_size = sizeof(buff2);
    bsp_uart_init(&huart1);

    while (1)
    {
        if(bsp_uart_available(&huart))
        {
            uint32_t size = bsp_uart_size(&huart);
            uint8_t* rbuf = bsp_uart_buff(&huart);
            bsp_uart_write(&huart, rbuf, size);
            bsp_uart_clear(&huart);
            LOGI("uart %d, recv size is %d", bsp_uart_port(&huart), size);
            ESP_LOG_BUFFER_HEX("UART Rev", rbuf, size);
        }

        if(bsp_uart_available(&huart1))
        {
            uint32_t size = bsp_uart_size(&huart1);
            uint8_t* rbuf = bsp_uart_buff(&huart1);
            bsp_uart_write(&huart1, rbuf, size);
            bsp_uart_clear(&huart1);
            LOGI("uart %d, recv size is %d", bsp_uart_port(&huart1), size);
            ESP_LOG_BUFFER_HEX("UART Rev", rbuf, size);
        }

        delay_ms(100);
    }
} 


