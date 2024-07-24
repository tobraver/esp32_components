#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "chip_ota.h"

void app_main(void)
{
    chip_ota_init();
    chip_ota_load_updater_file();
    chip_ota_load_frameware_file();

    while (1)
    {
        if(chip_ota_boodloader_handshake() == false) {
            
        } else {
            if(chip_ota_bootloader_send_agent() == false) {
                break;
            }
            if(chip_ota_updater_prepare_upgrade() == false) {
                break;
            }
            if(chip_ota_updater_get_partition() == false) {                
                break;
            } else {
                uint32_t verify_res = chip_ota_updater_verify_partition();
                if(chip_ota_updater_need_upgrade(verify_res) == false) {
                    break;
                }
                if(chip_ota_updater_update_partition_table() == false) {
                    break;
                }
                if(chip_ota_updater_user_partition(verify_res) == false) {
                    break;
                }
                if(chip_ota_updater_asr_partition(verify_res) == false) {
                    break;
                }
                if(chip_ota_updater_dnn_partition(verify_res) == false) {
                    break;
                }
                if(chip_ota_updater_voice_partition(verify_res) == false) {
                    break;
                }
                if(chip_ota_updater_user_file_partition(verify_res) == false) {
                    break;
                }
            }
            break;
        }
    }    
    chip_ota_updater_exit_upgrade();
    printf("升级结束\n");
}


