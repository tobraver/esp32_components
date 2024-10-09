#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"

#include "rb_log.h"

void app_main(void)
{
    rb_log_init();

    for(int i = 0; i < 300; i++) {
        ESP_LOGI("wifi", "hello world [%d]", i);
    }

    vTaskDelay(1000);
    printf("-------->1 msg num: %d\n", rb_log_get_msg_num());
    for(int i = 0; i < 300; i++) {
        char* msg = rb_log_get_msg();
        if(msg) {
            printf("msg: %s", msg);
            rb_log_free_msg(msg);
        }
    }
    
    printf("-------->2 msg num: %d\n", rb_log_get_msg_num());
    for(int i = 0; i < 300; i++) {
        ESP_LOGI("wifi", "hello world hello world [%d]", i);
    }

    vTaskDelay(1000);
    printf("-------->3 msg num: %d\n", rb_log_get_msg_num());
    for(int i = 0; i < 300; i++) {
        char* msg = rb_log_get_msg();
        if(msg) {
            printf("msg: %s", msg);
            rb_log_free_msg(msg);
        }
    }

    printf("-------->4 msg num: %d\n", rb_log_get_msg_num());
    for(int i = 0; i < 300; i++) {
        ESP_LOGI("wifi", "hello world hello world [%d]", i);
    }

    vTaskDelay(1000);
    printf("-------->5 msg num: %d\n", rb_log_get_msg_num());
    for(int i = 0; i < 300; i++) {
        char* msg = rb_log_get_msg();
        if(msg) {
            printf("msg: %s", msg);
            rb_log_free_msg(msg);
        }
    }

    printf("-------->6 msg num: %d\n", rb_log_get_msg_num());
    for(int i = 0; i < 300; i++) {
        ESP_LOGI("wifi", "hello world [%d]", i);
    }

    vTaskDelay(1000);
    printf("-------->7 msg num: %d\n", rb_log_get_msg_num());
    for(int i = 0; i < 300; i++) {
        char* msg = rb_log_get_msg();
        if(msg) {
            printf("msg: %s", msg);
            rb_log_free_msg(msg);
        }
    }
}
