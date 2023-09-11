#include "bsp_uart.h"
#include "esp_log.h"

static const char *TAG = "UART";

static esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint32_t time_ms)
{
    esp_timer_stop(timer);
    return esp_timer_start_once(timer, time_ms*1000);
}

void bsp_uart_lock(bsp_uart_t* huart)
{
    xSemaphoreTake(huart->conf.mutex, portMAX_DELAY);
}

void bsp_uart_unlock(bsp_uart_t* huart)
{
    xSemaphoreGive(huart->conf.mutex);
}

static void bsp_uart_recv_task(void *pvParameters)
{
    bsp_uart_t* huart = (bsp_uart_t*)pvParameters;
    uart_event_t rev_event;
    
    while (true)
    {
        uart_port_t rev_uart = huart->conf.uartx;
        QueueHandle_t rev_queue = huart->conf.queue;
        esp_timer_handle_t rev_timer = huart->conf.timer;

        ESP_LOGI(TAG, "uart %d wait for recv!", huart->conf.uartx);
        // block for recv!
        if(xQueueReceive(rev_queue, (void*)&rev_event, (portTickType)portMAX_DELAY)) 
        {
            switch(rev_event.type) 
            {
            case UART_DATA: 
            {
                if(huart->available == false)
                {
                    bsp_uart_lock(huart);

                    esp_timer_restart(rev_timer, huart->conf.timeout);
                    
                    uint32_t remain_size = huart->conf.buf_size - huart->size;
                    size_t rev_size = rev_event.size < remain_size ? rev_event.size : remain_size;
                    
                    if(rev_size)
                    {
                        uart_read_bytes(rev_uart, &huart->buff[huart->size], rev_size, portMAX_DELAY);
                        huart->size += rev_size;
                    }

                    if((rev_size < 120) || (huart->size == huart->conf.buf_size)) // one packet or full!
                    {
                        huart->available = true;
                        esp_timer_stop(rev_timer);
                    }

                    bsp_uart_unlock(huart);
                }
                else
                {
                    uart_flush_input(rev_uart);
                    xQueueReset(rev_queue);
                }
            } break;
            case UART_FIFO_OVF: 
            {
                ESP_LOGE(TAG, "uart %d hw fifo overflow", rev_uart);
                uart_flush_input(rev_uart);
                xQueueReset(rev_queue);
            } break;
            case UART_BUFFER_FULL: 
            {
                ESP_LOGE(TAG, "uart %d ring buffer full", rev_uart);
                uart_flush_input(rev_uart);
                xQueueReset(rev_queue);
            } break;
            case UART_BREAK: 
            {
                ESP_LOGE(TAG, "uart %d rx break", rev_uart);
            } break;
            case UART_PARITY_ERR: 
            {
                ESP_LOGE(TAG, "uart %d parity error", rev_uart);
            } break;
            case UART_FRAME_ERR: 
            {
                ESP_LOGE(TAG, "uart %d frame error", rev_uart);
            } break;
            default:
                break;
            }
        }
    }

    ESP_LOGE(TAG, "--@@@ uart %d recv task exit! @@@--", huart->conf.uartx);
    vTaskDelete(NULL);
}

void bsp_uart_timer_callback(void *arg) 
{
    bsp_uart_t* huart = (bsp_uart_t*)arg;
    bsp_uart_lock(huart);
    huart->available = true;
    bsp_uart_unlock(huart);
}

void bsp_uart_timer_init(bsp_uart_t* huart)
{
    esp_timer_create_args_t conf = {};
    conf.callback = &bsp_uart_timer_callback,   // 回调函数
    conf.arg = huart,
	esp_timer_create(&conf, &huart->conf.timer);
}

void bsp_uart_mutex_init(bsp_uart_t* huart)
{
    huart->conf.mutex = xSemaphoreCreateMutex();
}

// init uart with config
void bsp_uart_init(bsp_uart_t* huart)
{
    uart_config_t uart_config = {0};
    uart_config.baud_rate = huart->conf.baud_rate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity =  UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;
    uart_param_config(huart->conf.uartx, &uart_config);
    uart_set_pin(huart->conf.uartx, huart->conf.tx_io_num, huart->conf.rx_io_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(huart->conf.uartx, 128*4, 128*4, 20, &huart->conf.queue, 0);
    bsp_uart_timer_init(huart);
    bsp_uart_mutex_init(huart);
    xTaskCreate(bsp_uart_recv_task, "uart_recv", 1024*3, huart, 2, NULL);
}

// uart write buff
void bsp_uart_write(bsp_uart_t* huart, void* buff, uint32_t size)
{
    bsp_uart_lock(huart);
    uart_write_bytes(huart->conf.uartx, buff, size);
    uart_wait_tx_done(huart->conf.uartx, portMAX_DELAY);
    bsp_uart_unlock(huart);
}

// uart recv buff size
uint32_t bsp_uart_size(bsp_uart_t* huart)
{
    return huart->size;
}

// uart recv buff
uint8_t* bsp_uart_buff(bsp_uart_t* huart)
{
    return huart->buff;
}

// uart recv available
bool bsp_uart_available(bsp_uart_t* huart)
{
    return huart->available;
}

// uart recv wait for available! 
bool bsp_uart_wait(bsp_uart_t* huart, uint32_t time)
{
    while (time--)
    {
        if(bsp_uart_available(huart))
        {
            return true;
        }
        
        vTaskDelay(1/portTICK_PERIOD_MS); // 1ms
    }
    return false;
}

// uart recv clear
void bsp_uart_clear(bsp_uart_t* huart)
{
    bsp_uart_lock(huart);
    huart->size = 0;
    huart->available = false;
    bsp_uart_unlock(huart);
}

// uart recv port
uint32_t bsp_uart_port(bsp_uart_t* huart)
{
    return huart->conf.uartx;
}

void bsp_uart_set_buad(bsp_uart_t* huart, uint32_t buad)
{
    if(uart_set_baudrate(huart->conf.uartx, buad) == ESP_OK)
    {
        bsp_uart_lock(huart);
        huart->conf.baud_rate = buad;
        bsp_uart_unlock(huart);
    }
}

