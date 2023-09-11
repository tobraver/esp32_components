#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "stdio.h"
#include "stdint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_timer.h"

typedef struct 
{
    int tx_io_num; // uart tx pin num
    int rx_io_num; // uart rx pin num
    int baud_rate; // uart baud rate
    int buf_size;  // uart recv buff size
    int timeout;   // uart recv buff timeout
    uart_port_t uartx; // uart for recv [x*120 bytes]
    QueueHandle_t queue; // uart recv queue
    esp_timer_handle_t timer; // uart recv timer
    SemaphoreHandle_t mutex; // uart mutex
} bsp_uart_conf;

typedef struct {
    uint8_t*  buff;      // uart recv buff
    uint32_t  size;      // uart recv size
    bool      available; // uart available
    bsp_uart_conf conf;  // uart config
} bsp_uart_t;

void bsp_uart_init(bsp_uart_t* huart);
void bsp_uart_write(bsp_uart_t* huart, void* buff, uint32_t size);
uint32_t bsp_uart_size(bsp_uart_t* huart);
uint8_t* bsp_uart_buff(bsp_uart_t* huart);
bool bsp_uart_available(bsp_uart_t* huart);
bool bsp_uart_wait(bsp_uart_t* huart, uint32_t time);
void bsp_uart_clear(bsp_uart_t* huart);
uint32_t bsp_uart_port(bsp_uart_t* huart);
void bsp_uart_set_buad(bsp_uart_t* huart, uint32_t buad);

#endif // !__BSP_UART_H__



