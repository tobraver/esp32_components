#ifndef __CHIP_OTA_H__
#define __CHIP_OTA_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

/**
 * @brief uart config
 */
#define CHIP_OTA_UART_NUM   1
#define CHIP_OTA_TX_IO_NUM  16
#define CHIP_OTA_RX_IO_NUM  17
#define CHIP_OTA_UART_BUFF  1024*5
#define CHIP_OTA_UART_BAUD  115200
#define CHIP_OTA_DEBUGE_EN  0

#ifdef __cplusplus
extern "C" {
#endif

void chip_ota_init(void);
bool chip_ota_load_updater_file(void);
bool chip_ota_load_frameware_file(void);
bool chip_ota_boodloader_handshake(void);
bool chip_ota_bootloader_send_agent(void);
bool chip_ota_updater_prepare_upgrade(void);
bool chip_ota_updater_get_partition(void);
uint32_t chip_ota_updater_verify_partition(void);
bool chip_ota_updater_need_upgrade(uint32_t verify_res);
bool chip_ota_updater_update_partition_table(void);
bool chip_ota_updater_user_partition(uint32_t verify_res);
bool chip_ota_updater_asr_partition(uint32_t verify_res);
bool chip_ota_updater_dnn_partition(uint32_t verify_res);
bool chip_ota_updater_voice_partition(uint32_t verify_res);
bool chip_ota_updater_user_file_partition(uint32_t verify_res);
bool chip_ota_updater_exit_upgrade(void);

#ifdef __cplusplus
}
#endif
#endif // !__CHIP_OTA_H__
