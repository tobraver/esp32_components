#include "chip_ota.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#define OTA_BOOTLOADER_BAUDRATE             115200 // 最大921600 (目前只支持115200)
#define OTA_UPDATER_BAUDRATE                921600 // 最大2000000

#define OTA_CLOUD_SIZE                      4096*10 //>=MAX_DATA_LENGTH ,且能整除4096
#define MAX_DATA_LENGTH                     1024*4 //必须大于等于32，小于等于4096，且能整除4096
#define MAX_PACKAGE_LENGTH                  (MAX_DATA_LENGTH + 10)
#define MIN_PARTITION_SIZE                  4096
#define ERASE_BLOCK_SIZE                    4096

//MESSAGE TYPE
#define MSG_TYPE_CMD	                    0xA0
#define MSG_TYPE_REQ	                    0xA1
#define MSG_TYPE_ACK	                    0xA2
#define MSG_TYPE_NOTIFY	                    0xA3

//MESSAGE CMD
#define MSG_CMD_UPDATE_REQ                  0x03 //握手消息
#define MSG_CMD_GET_INFO                    0x04 //获取分区表信息
#define MSG_CMD_UPDATE_CHECK_READY          0x05 //检测设备是否重启成功
#define MSG_CMD_UPDATE_VERIFY_INFO          0x06 //发送UPDATE信息
#define MSG_CMD_UPDATE_ERA                  0x07
#define MSG_CMD_UPDATE_WRITE                0x08
#define MSG_CMD_UPDATE_BLOCK_WRITE_DONE     0x09
#define MSG_CMD_UPDATE_VERIFY               0x0A
#define MSG_CMD_CHANGE_BAUDRATE             0x0B
#define MSG_CMD_TEST_BAUDRATE               0x0C
#define MSG_CMD_PROGRESS                    0x11
#define MSG_CMD_SET_FW_FMT_VER              0x12 //设置固件格式版本号，1个字节数据即版本号

#define MSG_CMD_SYS_RST                     0xA1 //复位命令

#define PARTITION_USER1_FLAG_MASK           0x0001 //需要升级user code 1
#define PARTITION_USER2_FLAG_MASK           0x0002 //需要升级user code 2
#define PARTITION_ASR_FLAG_MASK             0x0004 //需要升级ASR
#define PARTITION_DNN_FLAG_MASK             0x0008 //需要升级DNN
#define PARTITION_VOICE_FLAG_MASK           0x0010 //需要升级voice
#define PARTITION_USERFILE_FLAG_MASK        0x0020 //需要升级user file
#define PARTITION_UPDATE_ALL				(0x0001|0x0002|0x0004|0x0008|0x0010|0x0020)
#define PARTITION_INVALID_FILE_MASK         0x8000 //无效的固件文件

#define PARTITION_TABLE1_START_ADDR          0x6000 //分区表起始地址
#define PARTITION_TABLE2_START_ADDR          0x8000 //分区表起始地址
#define PROGRAM_AGENT_ADDR                   0x1ff58000 //3代updater运行地址
#define GET_LWORD(p)    ((*(unsigned char*)(p)) + ((*(unsigned char*)((p)+1))<<8) + ((*(unsigned char*)((p)+2))<<16) + ((*(unsigned char*)((p)+3))<<24))
#define GET_SWORD(p)    ((*(unsigned char*)(p)) + ((*(unsigned char*)((p)+1))<<8))

#pragma pack(1)   //单字节对齐

//分区信息结构体
typedef struct
{
    unsigned int version;     //分区版本
    unsigned int address;     //分区起始地址
    unsigned int size;        //分区大小
    unsigned int crc;         //分区CRC16校验
    unsigned char status;     //分区当前状态 0xF0-分区有效  0xFC-需要更新的分区  0xC0-无效分区
}partition_info_t;

//分区表结构体
typedef struct
{
    unsigned int ManufacturerID; //厂商ID
    unsigned int ProductID[2]; //产品ID

    unsigned int HWName[16]; //硬件名称
    unsigned int HWVersion; //硬件版本
    unsigned int SWName[16]; //软件名称
    unsigned int SWVersion; //软件版本

    unsigned int BootLoaderVersion; //bootloader版本
    char         ChipName[9]; //芯片名称
    uint8_t      FirmwareFormatVer; //固件格式版本
    uint8_t      reserve[4]; //预留

    partition_info_t user_code1; //代码分区1信息
    partition_info_t user_code2; //代码分区2信息
    partition_info_t asr_cmd_model; //asr分区信息
    partition_info_t dnn_model; //dnn分区信息
    partition_info_t voice; //voice分区信息
    partition_info_t user_file; //user_file分区信息

    unsigned int     ConsumerDataStartAddr; //nv_data分区起始地址-ota无需关注
    unsigned int     ConsumerDataSize; //nv_data分区大小 -ota无需关注
    unsigned short PartitionTableChecksum; //分区表校验值
}partition_table_t;

//固件格式版本
typedef enum
{
    FW_FMT_VER_1 = 1, //固件格式版本1，现用于CI110X SDK和CI130X_SDK、CI230X_SDK、 CI231X_SDK
    FW_FMT_VER_2, //固件格式版本2，现用于CI110X_SDK_Lite和CI112X_SDK
    FW_FMT_VER_MAX,
} fw_fmt_ver_t;

#pragma pack()

/**
 * @brief .radata section in flash
 * 
 * @note will apppend 0x00 to end
 */
extern const uint8_t ci130x_updater_bin_start[] asm("_binary_ci130x_updater_bin_start");
extern const uint8_t ci130x_updater_bin_end[]   asm("_binary_ci130x_updater_bin_end");

extern const uint8_t xiaoai_bin_start[] asm("_binary_xiaoai_bin_start");
extern const uint8_t xiaoai_bin_end[]   asm("_binary_xiaoai_bin_end");

static const char *TAG = "chip_ota";

bool chip_ota_uart_init(void);
bool chip_ota_uart_deinit(void);

typedef struct {
    const uint8_t* updater;
    uint32_t updater_size;
    uint8_t* frameware;
    uint32_t frameware_size;
    partition_table_t frameware_partition;
    partition_table_t device_partition;
    partition_table_t update_partition;
    uint16_t calculate_crc;
} chip_ota_desc_t;

static chip_ota_desc_t s_ota_desc;

void chip_ota_init(void)
{
    chip_ota_uart_init();
}

bool chip_ota_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = CHIP_OTA_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_driver_install(CHIP_OTA_UART_NUM, CHIP_OTA_UART_BUFF, 0, 0, NULL, 0);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart driver install failed, error: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_param_config(CHIP_OTA_UART_NUM, &uart_config);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart param config failed, error: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_set_pin(CHIP_OTA_UART_NUM, CHIP_OTA_TX_IO_NUM, CHIP_OTA_RX_IO_NUM, -1, -1);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart set pin failed, error: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "uart driver install success");
    return true;
}

bool chip_ota_uart_deinit(void)
{
    esp_err_t err = uart_driver_delete(CHIP_OTA_UART_NUM);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "uart driver delete failed, error: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "uart driver delete success");
    return true;
}

bool chip_ota_uart_set_baud(uint32_t baud_rate)
{
    esp_err_t err = uart_set_baudrate(CHIP_OTA_UART_NUM, baud_rate);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "set baud rate %u failed, error: %s", baud_rate, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "set baud rate %u success", baud_rate);
    }
    return err == ESP_OK ? true : false;
}

bool chip_ota_uart_clear(void)
{
    return uart_flush_input(CHIP_OTA_UART_NUM) == ESP_OK ? true : false;
}

bool chip_ota_uart_send(uint8_t *buff, uint32_t len)
{
    if(buff == NULL || len == 0) {
        return true;
    }
    int ret = uart_write_bytes(CHIP_OTA_UART_NUM, buff, len);
    if(ret < 0) {
        ESP_LOGE(TAG, "uart send failed, error: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool chip_ota_uart_send_wait_done(void)
{
    return uart_wait_tx_done(CHIP_OTA_UART_NUM, portMAX_DELAY) == ESP_OK ? true : false;
}

int chip_ota_uart_recv(uint8_t *buff, uint32_t len, uint32_t timeout)
{
    return uart_read_bytes(CHIP_OTA_UART_NUM, buff, len, pdMS_TO_TICKS(timeout));
}

void chip_ota_delay_ms(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

//crc16校验静态表
static const uint16_t crc16tab_ccitt[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

uint16_t chip_ota_get_crc(uint16_t crc, uint8_t *buf, uint32_t len)
{
    uint32_t counter;
    for (counter = 0; counter < len; counter++)
    {
        unsigned char t = *(unsigned char *)buf++;
        crc = (crc << 8) ^ crc16tab_ccitt[((crc >> 8) ^ t) & 0x00FF];
    }
    return crc;
}

void chip_ota_send_cmd(uint8_t msg_type, uint8_t cmd, uint8_t seq, uint8_t *data, uint32_t data_len)
{
    uint8_t buff[32] = {0};
    uint32_t len = 0;
    uint16_t crc = 0;
    // head
    buff[len++] = 0xa5;
    buff[len++] = 0x0f;
    // len
    buff[len++] = data_len&0xff;
    buff[len++] = (data_len>>8)&0xff;
    // msg type
    buff[len++] = msg_type;
    // cmd
    buff[len++] = cmd;
    // seq
    buff[len++] = seq;
    // crc
    crc = chip_ota_get_crc(crc, &buff[4], 3);
    crc = chip_ota_get_crc(crc, data, data_len);
    buff[len++] = crc&0xff;
    buff[len++] = (crc>>8)&0xff;
    // tail
    buff[len++] = 0xff;

    chip_ota_uart_clear();
    chip_ota_uart_send(&buff[0], 7);
    chip_ota_uart_send(data, data_len);
    chip_ota_uart_send(&buff[7], len - 7);
    chip_ota_uart_send_wait_done();

    if(data_len < 512)
    {
        printf("send cmd: ");
        for(uint32_t i = 0; i < 7; i++) {
            printf("%02X ", buff[i]);
        }
        for(uint32_t i = 0; i < data_len; i++) {
            printf("%02X ", data[i]);
        }
        for(uint32_t i = 7; i < len; i++) {
            printf("%02X ", buff[i]);
        }
        printf("\n");
    }
}

bool chip_ota_recv_packet_succ(uint8_t* buff, uint32_t len, uint32_t msg_dlen, uint8_t msg_type, uint8_t cmd, uint8_t seq, uint8_t* msg_data)
{
    if(buff[0] != 0xa5 || buff[1] != 0x0f) {
        ESP_LOGE(TAG, "head error");
        return false;
    }
    if(buff[len-1] != 0xff) {
        ESP_LOGE(TAG, "tail error");
        return false;
    }
    uint16_t dlen = (buff[3]<<8) | buff[2];
    if(dlen != (msg_dlen & 0xffff)) {
        ESP_LOGE(TAG, "msg data len error");
        return false;
    }
    if(buff[4] != msg_type) {
        ESP_LOGE(TAG, "msg type error");
        return false;
    }
    if(buff[5] != cmd) {
        ESP_LOGE(TAG, "cmd error");
        return false;
    }
    if(buff[6] != seq) {
        ESP_LOGE(TAG, "seq error");
        return false;
    }
    uint16_t crc1 = chip_ota_get_crc(0, &buff[4], len - 7);
    uint16_t crc2 = (buff[len-2]<<8) | buff[len-3];
    if(crc1 != crc2) {
        ESP_LOGE(TAG, "crc error, crc1: %04X, crc2: %04X", crc1, crc2);
        return false;
    }
    if(msg_dlen && msg_data) {
        memcpy(msg_data, &buff[7], msg_dlen);
    }
    return true;
}

bool chip_ota_recv_cmd(uint32_t msg_dlen, uint8_t msg_type, uint8_t cmd, uint8_t seq, uint8_t *msg_data, uint32_t timeout)
{
    uint32_t want_len = msg_dlen + 10;
    uint8_t* buff = (uint8_t*)calloc(1, want_len);
    int ret = chip_ota_uart_recv(buff, want_len, timeout);
    if(ret < 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        if(ret != want_len) {
            ESP_LOGE(TAG, "recv len error, len %d, want %d", ret, want_len);
            ret = -1;
        } else {
            ESP_LOG_BUFFER_HEX("recv", buff, ret);
            ret = chip_ota_recv_packet_succ(buff, ret, msg_dlen, msg_type, cmd, seq, msg_data) ? 0 : -1;
        }
    }
    free(buff);
    return ret < 0 ? false : true;
}

bool chip_ota_boodloader_handshake(void)
{
    chip_ota_uart_set_baud(OTA_UPDATER_BAUDRATE);
    ESP_LOGI(TAG, "## Bootloader握手");
    uint32_t try_cnt = 200;
    while (try_cnt) {
        chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_UPDATE_REQ, 0x00, NULL, 0);
        if(chip_ota_recv_cmd(0x00, MSG_TYPE_ACK, MSG_CMD_UPDATE_REQ, 0x00, NULL, 10)) {
            break;
        }
        try_cnt--;
        chip_ota_delay_ms(10);
    }
    if(try_cnt) {
        ESP_LOGW(TAG, "## BootLoader握手成功");
    } else {
        ESP_LOGE(TAG, "## BootLoader握手失败");
    }
    return try_cnt > 0 ? true : false;
}

bool chip_ota_load_updater_file(void)
{
    s_ota_desc.updater = (const uint8_t*)ci130x_updater_bin_start;
    s_ota_desc.updater_size = ci130x_updater_bin_end - ci130x_updater_bin_start - 1;
    ESP_LOGI(TAG, "## 加载updater镜像成功, 大小 %d", s_ota_desc.updater_size);
    // ESP_LOG_BUFFER_HEX(TAG, s_ota_desc.updater, s_ota_desc.updater_size);
    return true;
}

uint16_t chip_ota_get_updater_file_crc(uint8_t *file, uint32_t size)
{
    uint32_t updater_size = size;
    uint32_t erase_size = (updater_size + MIN_PARTITION_SIZE - 1) / MIN_PARTITION_SIZE * MIN_PARTITION_SIZE;
    uint16_t updater_crc = 0;

    updater_crc = chip_ota_get_crc(updater_crc, file, size);
    for (uint32_t i = 0; i < erase_size - updater_size; i++)
    {
        uint8_t fill_byte = 0xFF;
        updater_crc = chip_ota_get_crc(updater_crc, &fill_byte, sizeof(fill_byte));
    }
    ESP_LOGI(TAG, "## updater镜像CRC: %04X", updater_crc);
    return updater_crc;
}

bool chip_ota_bootloader_send_updater_file(uint32_t offset, uint32_t size)
{
    if(s_ota_desc.updater == NULL || s_ota_desc.updater_size == 0) {
        ESP_LOGE(TAG, "## updater镜像未加载");
        return false;
    }
    uint32_t len = 4 + 4096;
    uint8_t* buff = (uint8_t*)malloc(len);
    if(buff == NULL) {
        ESP_LOGE(TAG, "## 申请updater数据空间内存失败");
        return false;
    }

    memset(buff, 0xff, len); // defult fill 0xff
    buff[0] = offset&0xff; // offset
    buff[1] = (offset>>8)&0xff;
    buff[2] = (offset>>16)&0xff;
    buff[3] = (offset>>24)&0xff;

    int32_t remain = s_ota_desc.updater_size - offset;
    remain = remain < size ? remain : size;
    if(remain > 0) {
        memcpy(buff+4, s_ota_desc.updater + offset, remain);
    }

    chip_ota_send_cmd(MSG_TYPE_ACK, MSG_CMD_UPDATE_WRITE, 0x00, buff, len);
    free(buff);
    return true;
}

/**
 * @brief 发送updater镜像信息
 * 
 * @note 在 chip_ota_boodloader_handshake 成功后调用
 */
bool chip_ota_bootloader_send_updater_info(void)
{
    ESP_LOGI(TAG, "## 发送updater镜像信息");
    chip_ota_load_updater_file();
    uint32_t updater_exec_addr = PROGRAM_AGENT_ADDR;
    uint32_t updater_erase_size = (s_ota_desc.updater_size + MIN_PARTITION_SIZE - 1) / MIN_PARTITION_SIZE * MIN_PARTITION_SIZE;
    uint16_t updater_crc_val = chip_ota_get_updater_file_crc((uint8_t*)s_ota_desc.updater, s_ota_desc.updater_size);
    uint8_t buff[32] = { 0 };
    uint32_t len = 0;

    buff[len++] = updater_exec_addr&0xff;
    buff[len++] = (updater_exec_addr>>8)&0xff;
    buff[len++] = (updater_exec_addr>>16)&0xff;
    buff[len++] = (updater_exec_addr>>24)&0xff;
    buff[len++] = updater_erase_size&0xff;
    buff[len++] = (updater_erase_size>>8)&0xff;
    buff[len++] = (updater_erase_size>>16)&0xff;
    buff[len++] = (updater_erase_size>>24)&0xff;
    buff[len++] = updater_crc_val&0xff;
    buff[len++] = (updater_crc_val>>8)&0xff;

    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_UPDATE_VERIFY_INFO, 0x01, buff, len);
    if(chip_ota_recv_cmd(0x00, MSG_TYPE_ACK, MSG_CMD_UPDATE_VERIFY_INFO, 0x00, NULL, 100) == false) {
        ESP_LOGE(TAG, "## 发送updater镜像信息失败");
        return false;
    }
    ESP_LOGI(TAG, "## 发送updater镜像信息成功");
    return true;
}

bool chip_ota_bootloader_send_updater(void)
{
    uint8_t buff[32] = { 0 };
    uint32_t len = 0, ticks = 0;
    uint8_t is_finish = 0, cmd = 0;
    ESP_LOGI(TAG, "## 等待设备获取updater镜像信息");

    while (1) {
        memset(buff, 0x00, sizeof(buff));
        if(is_finish) {
            len = 0x00; // 传输完成请求
            cmd = MSG_CMD_UPDATE_BLOCK_WRITE_DONE;
        } else {
            len = 0x08; // 传输数据包请求
            cmd = MSG_CMD_UPDATE_WRITE;
        }
        if(chip_ota_recv_cmd(len, MSG_TYPE_REQ, cmd, 0x00, buff, 200) == false) {
            // ESP_LOGE(TAG, "## 等待设备请求updater数据帧超时");
            ticks++;
        } else {
            ticks = 0;
            if(len == 0x00) {
                ESP_LOGI(TAG, "## 设备请求updater数据帧结束");
                break;
            } else {
                uint32_t offset = buff[3]<<24 | buff[2]<<16 | buff[1]<<8 | buff[0];
                uint32_t size = buff[7]<<24 | buff[6]<<16 | buff[5]<<8 | buff[4];
                ESP_LOGI(TAG, "## 设备请求updater数据帧, 请求地址: 0x%X, 请求大小: %d", offset, size);
                chip_ota_bootloader_send_updater_file(offset, size);
                if(offset + size >= s_ota_desc.updater_size) {
                    is_finish = 0x01;
                }
            }
        }
        if(ticks >= 10) { // 10 * 200 = 2000ms
            ESP_LOGE(TAG, "## 等待设备请求updater数据帧超时");
            return false;
        }
    }
    ESP_LOGI(TAG, "## 设备获取updater镜像信息完成");
    return true;
}

bool chip_ota_bootloader_send_updater_verify(void)
{
    uint8_t buff[32] = { 0 };
    uint32_t len = 1;
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_UPDATE_VERIFY, 0x01, NULL, 0);
    if(chip_ota_recv_cmd(len, MSG_TYPE_ACK, MSG_CMD_UPDATE_VERIFY, 0x00, buff, 100) == false) {
        ESP_LOGE(TAG, "## 等待验证updater镜像信息失败");
        return false;
    }
    if(buff[0] != 0x01) {
        ESP_LOGE(TAG, "## updater验证镜像信息失败");
        return false;
    }
    ESP_LOGI(TAG, "## updater验证镜像信息成功");
    if(chip_ota_recv_cmd(0, MSG_TYPE_REQ, MSG_CMD_UPDATE_REQ, 0x00, NULL, 100) == false) {
        ESP_LOGE(TAG, "## 等待updater运行上报失败");
        return false;
    }
    return true;
}

bool chip_ota_bootloader_send_agent(void)
{
    if(chip_ota_bootloader_send_updater_info() == false) {
        return false;
    }
    if(chip_ota_bootloader_send_updater() == false) {
        return false;
    }
    if(chip_ota_bootloader_send_updater_verify() == false) {
        false;
    }
    return true;
}

bool chip_ota_updater_handshark(void)
{
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_UPDATE_CHECK_READY, 0x01, NULL, 0);
    if(chip_ota_recv_cmd(0x00, MSG_TYPE_ACK, MSG_CMD_UPDATE_CHECK_READY, 0x00, NULL, 100) == false) {
        ESP_LOGE(TAG, "## updater握手失败");
        return false;
    }
    ESP_LOGI(TAG, "## updater握手成功");
    return true;
}

bool chip_ota_updater_change_baudrate(void)
{
    uint32_t buad = OTA_UPDATER_BAUDRATE;
    uint8_t buff[4] = { 0 };
    buff[0] = buad&0xff;
    buff[1] = (buad>>8)&0xff;
    buff[2] = (buad>>16)&0xff;
    buff[3] = (buad>>24)&0xff;
    
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_CHANGE_BAUDRATE, 0x01, buff, sizeof(buff));
    if(chip_ota_recv_cmd(0x00, MSG_TYPE_ACK, MSG_CMD_CHANGE_BAUDRATE, 0x00, NULL, 100) == false) {
        ESP_LOGE(TAG, "## updater切换波特率失败");
        return false;
    }
    ESP_LOGI(TAG, "## updater切换波特率成功, 波特率: %u", buad);
    return true;
}

bool chip_ota_updater_set_frameware(void)
{
    uint8_t buff[1] = { 0 };
    buff[0] = FW_FMT_VER_1;
    
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_SET_FW_FMT_VER, 0x01, buff, sizeof(buff));
    if(chip_ota_recv_cmd(0x00, MSG_TYPE_ACK, MSG_CMD_SET_FW_FMT_VER, 0x00, NULL, 100) == false) {
        ESP_LOGE(TAG, "## updater设置固件版本失败");
        return false;
    }
    ESP_LOGI(TAG, "## updater设置固件版本成功, 版本号: %u", buff[0]);
    return true;
}

bool chip_ota_updater_prepare_upgrade(void)
{
    if(chip_ota_updater_handshark() == false) {
        return false;
    }
    if(chip_ota_updater_change_baudrate() == false) {
        return false;
    }
    if(chip_ota_updater_handshark() == false) {
        return false;
    }
    if(chip_ota_updater_set_frameware() == false) {
        return false;
    }
    return true;
}

void chip_ota_partition_info_print(partition_table_t* partition)
{
    printf("ManufacturerID = %u\n", partition->ManufacturerID);
    printf("DeviceID = %u\n", partition->ProductID[0]);
    printf("HWName = %s\n", (char*)partition->HWName);
    printf("HWVersion = %u.%u.%u\n", (partition->HWVersion>>16)&0xff, (partition->HWVersion>>8)&0xff, partition->HWVersion&0xff);
    printf("SWName = %s\n", (char*)partition->SWName);
    printf("SWVersion = %u.%u.%u\n", (partition->SWVersion>>16)&0xff, (partition->SWVersion>>8)&0xff, partition->SWVersion&0xff);
    printf("BootLoaderVersion = %u.%u\n", (partition->BootLoaderVersion>>8)&0xff, partition->BootLoaderVersion&0xff);
    printf("ChipName = %s\n", (char*)partition->ChipName);
    printf("FirmwareFormatVer = %u\n", partition->FirmwareFormatVer);
    printf("reserve = %u.%u.%u.%u\n", partition->reserve[0], partition->reserve[1], partition->reserve[2], partition->reserve[3]);

    printf("user_code1.version = %u\n", partition->user_code1.version);
    printf("user_code1.address = 0x%X\n", partition->user_code1.address);
    printf("user_code1.size = 0x%X\n", partition->user_code1.size);
    printf("user_code1.crc = 0x%08X\n", partition->user_code1.crc);
    printf("user_code1.status = 0x%02X\n", partition->user_code1.status);
    
    printf("user_code2.version = %u\n", partition->user_code2.version);
    printf("user_code2.address = 0x%X\n", partition->user_code2.address);
    printf("user_code2.size = 0x%X\n", partition->user_code2.size);
    printf("user_code2.crc = 0x%08X\n", partition->user_code2.crc);
    printf("user_code2.status = 0x%02X\n", partition->user_code2.status);

    printf("asr_cmd_model.version = %u\n", partition->asr_cmd_model.version);
    printf("asr_cmd_model.address = 0x%X\n", partition->asr_cmd_model.address);
    printf("asr_cmd_model.size = 0x%X\n", partition->asr_cmd_model.size);
    printf("asr_cmd_model.crc = 0x%08X\n", partition->asr_cmd_model.crc);
    printf("asr_cmd_model.status = 0x%02X\n", partition->asr_cmd_model.status);

    printf("dnn_model.version = %u\n", partition->dnn_model.version);
    printf("dnn_model.address = 0x%X\n", partition->dnn_model.address);
    printf("dnn_model.size = 0x%X\n", partition->dnn_model.size);
    printf("dnn_model.crc = 0x%08X\n", partition->dnn_model.crc);
    printf("dnn_model.status = 0x%02X\n", partition->dnn_model.status);
    
    printf("voice.version = %u\n", partition->voice.version);
    printf("voice.address = 0x%X\n", partition->voice.address);
    printf("voice.size = 0x%X\n", partition->voice.size);
    printf("voice.crc = 0x%08X\n", partition->voice.crc);
    printf("voice.status = 0x%02X\n", partition->voice.status);

    printf("user_file.version = %u\n", partition->user_file.version);
    printf("user_file.address = 0x%X\n", partition->user_file.address);
    printf("user_file.size = 0x%X\n", partition->user_file.size);
    printf("user_file.crc = 0x%08X\n", partition->user_file.crc);
    printf("user_file.status = 0x%02X\n", partition->user_file.status);


    printf("ConsumerDataStartAddr = 0x%X\n", partition->ConsumerDataStartAddr);
    printf("ConsumerDataSize = 0x%X\n", partition->ConsumerDataSize);
    printf("PartitionTableChecksum = 0x%04X\n", partition->PartitionTableChecksum);
}

bool chip_ota_load_frameware_file(void)
{
    s_ota_desc.frameware = (uint8_t*)xiaoai_bin_start;
    s_ota_desc.frameware_size = xiaoai_bin_end - xiaoai_bin_start - 1;
    ESP_LOGI(TAG, "## 加载frameware镜像成功, 大小 %d", s_ota_desc.updater_size);
    // ESP_LOG_BUFFER_HEX(TAG, s_ota_desc.updater, s_ota_desc.updater_size);

    if(s_ota_desc.frameware_size < sizeof(partition_table_t)) {
        ESP_LOGE(TAG, "## frameware镜像大小错误");
        return false;
    }

    uint32_t offset = PARTITION_TABLE2_START_ADDR;
    memcpy(&s_ota_desc.frameware_partition, s_ota_desc.frameware + offset, sizeof(partition_table_t));    
    // ESP_LOG_BUFFER_HEX("partition:", &s_ota_desc.frameware_partition, sizeof(s_ota_desc.frameware_partition));
    chip_ota_partition_info_print(&s_ota_desc.frameware_partition);
    return true;
}

//比较分区信息
int chip_ota_partition_compare(partition_info_t *p1, partition_info_t *p2)
{
    if (p1->version != p2->version ||
            p1->size != p2->size ||
            p1->address != p2->address ||
            p1->crc != p2->crc)
    {
        return 1;
    }
    return 0;
}

uint16_t chip_ota_partition_get_sum(partition_table_t *partition)
{
    int len = sizeof(partition_table_t) - 2;
    unsigned short sum = 0;
    unsigned char user_code1_status = partition->user_code1.status;
    unsigned char user_code2_status = partition->user_code2.status;
    unsigned char asr_cmd_model_status = partition->asr_cmd_model.status;
    unsigned char dnn_model_status = partition->dnn_model.status;
    unsigned char voice_status = partition->voice.status;
    unsigned char user_file_status = partition->user_file.status;

    partition->user_code1.status = 0xF0;
    partition->user_code2.status = (partition->FirmwareFormatVer == 1) ? 0xF0 : 0xFF;
    partition->asr_cmd_model.status = 0;
    partition->dnn_model.status = 0;
    partition->voice.status = 0;
    partition->user_file.status = 0;

    for (int i = 0; i < len; i++) {
        sum += ((unsigned char *)partition)[i];
    }
    partition->user_code1.status = user_code1_status;
    partition->user_code2.status = user_code2_status;
    partition->asr_cmd_model.status = asr_cmd_model_status;
    partition->dnn_model.status = dnn_model_status;
    partition->voice.status = voice_status;
    partition->user_file.status = user_file_status;
    return sum;
}

bool chip_ota_updater_get_partition(void)
{
    int len = 0x012E;
    uint8_t buff[len];
    memset(buff, 0, len);
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_GET_INFO, 0x01, NULL, 0);
    if(chip_ota_recv_cmd(len, MSG_TYPE_ACK, MSG_CMD_GET_INFO, 0x00, buff, 500) == false) {
        ESP_LOGE(TAG, "## updater获取分区表失败");
        return false;
    }
    ESP_LOGI(TAG, "## updater获取分区表成功");
    ESP_LOGI(TAG, "## 设备分区表详细信息:");
    memcpy(&s_ota_desc.device_partition, &buff[8], sizeof(partition_table_t));
    chip_ota_partition_info_print(&s_ota_desc.device_partition);
    return true;
}

uint32_t chip_ota_updater_verify_partition(void)
{
    uint32_t mask_res = 0;
    partition_table_t* frameware_partition = &s_ota_desc.frameware_partition;
    partition_table_t* device_partition = &s_ota_desc.device_partition;
    partition_table_t* update_partition = &s_ota_desc.update_partition;

    if(frameware_partition->PartitionTableChecksum != chip_ota_partition_get_sum(frameware_partition)) {
        ESP_LOGE(TAG, "## 升级文件分区表校验失败");
        return PARTITION_INVALID_FILE_MASK;
    }

    /*如果Device分区表被破坏或数据错误，直接整片升级*/
    if(device_partition->PartitionTableChecksum != chip_ota_partition_get_sum(device_partition)) {
        *update_partition = *frameware_partition;
        update_partition->user_code1.status = 0xFC;
        update_partition->user_code2.status = 0xFC;
        update_partition->asr_cmd_model.status = 0xFF;
        update_partition->dnn_model.status = 0xFF;
        update_partition->voice.status = 0xFF;
        update_partition->user_file.status = 0xFF;
        update_partition->PartitionTableChecksum = chip_ota_partition_get_sum(update_partition);
        ESP_LOGE(TAG, "## 分区表被破坏，升级所有内容");
        return PARTITION_UPDATE_ALL;
    }

    *update_partition = *device_partition;
    memcpy(update_partition, frameware_partition, 166);
    ESP_LOGI(TAG, "update_partition->SWName = %s", (char*)update_partition->SWName);
    ESP_LOGI(TAG, "update_partition->SWVersion = %u.%u.%u", (update_partition->SWVersion>>16)&0xff, (update_partition->SWVersion>>8)&0xff, update_partition->SWVersion&0xff);

    //*update_partition = *frameware_partition;
    if (device_partition->user_code1.status == 0xFC) { /*优先升级无效分区*/
        update_partition->user_code1 = frameware_partition->user_code1;
        update_partition->user_code2.status = 0xF0;
        update_partition->user_code1.status = 0xFC;
        ESP_LOGI(TAG, "Result: update USER1");
        mask_res |= PARTITION_USER1_FLAG_MASK;
    } else if (device_partition->user_code2.status == 0xFC) {
        update_partition->user_code2 = frameware_partition->user_code2;
        update_partition->user_code1.status = 0xF0;
        update_partition->user_code2.status = 0xFC;
        ESP_LOGI(TAG, "Result: update USER2");
        mask_res |= PARTITION_USER2_FLAG_MASK;
    } else { /*两个 CODE 分区全部有效*/
        /*code 变小优先升级code1， code变大优先升级code2*/
        if (device_partition->user_code1.size > frameware_partition->user_code1.size) {
            update_partition->user_code1 = frameware_partition->user_code1;
            update_partition->user_code2.status = 0xF0;
            update_partition->user_code1.status = 0xFC;
            ESP_LOGI(TAG, "Result: code 1 and 2 is valid, user become small, update USER1");
            mask_res |= PARTITION_USER1_FLAG_MASK;
        } else if (device_partition->user_code1.size < frameware_partition->user_code1.size) {
            update_partition->user_code2 = frameware_partition->user_code2;
            update_partition->user_code1.status = 0xF0;
            update_partition->user_code2.status = 0xFC;
            ESP_LOGI(TAG, "Result: code 1 and 2 is valid, user become large, update USER2");
            mask_res |= PARTITION_USER2_FLAG_MASK;
        } else { /*大小相等，根据内容判断是否需要升级*/
            if (chip_ota_partition_compare(&device_partition->user_code1, &frameware_partition->user_code1) != 0) {
                update_partition->user_code1 = frameware_partition->user_code1;
                update_partition->user_code2.status = 0xF0;
                update_partition->user_code1.status = 0xFC;
                ESP_LOGI(TAG, "Result: code 1 and 2 is valid and size no change, user1 content different update USER1");
                mask_res |= PARTITION_USER1_FLAG_MASK;
            }
        }
    }

    if (chip_ota_partition_compare(&device_partition->asr_cmd_model, &frameware_partition->asr_cmd_model) || device_partition->asr_cmd_model.status != 0x00) {
        update_partition->asr_cmd_model = frameware_partition->asr_cmd_model;
        update_partition->asr_cmd_model.status = 0xFF;
        ESP_LOGI(TAG, "Result: update ASR");
        mask_res |= PARTITION_ASR_FLAG_MASK;
    }

    if (chip_ota_partition_compare(&device_partition->dnn_model, &frameware_partition->dnn_model) || device_partition->dnn_model.status != 0x00) {
        update_partition->dnn_model = frameware_partition->dnn_model;
        update_partition->dnn_model.status = 0xFF;
        ESP_LOGI(TAG, "Result: update DNN");
        mask_res |= PARTITION_DNN_FLAG_MASK;
    }

    if (chip_ota_partition_compare(&device_partition->voice, &frameware_partition->voice) || device_partition->voice.status != 0x00) {
        update_partition->voice = frameware_partition->voice;
        update_partition->voice.status = 0xFF;
        ESP_LOGI(TAG, "Result: update VOICE");
        mask_res |= PARTITION_VOICE_FLAG_MASK;
    }

    if (chip_ota_partition_compare(&device_partition->user_file, &frameware_partition->user_file) || device_partition->user_file.status != 0x00) {
        update_partition->user_file = frameware_partition->user_file;
        update_partition->user_file.status = 0xFF;
        ESP_LOGI(TAG, "Result: update USERFILE");
        mask_res |= PARTITION_USERFILE_FLAG_MASK;
    }

    if (mask_res) {
        update_partition->PartitionTableChecksum = chip_ota_partition_get_sum(update_partition);
    }

    if(((mask_res & PARTITION_USER1_FLAG_MASK) != 0) || ((mask_res & PARTITION_USER2_FLAG_MASK) != 0)) {
        ESP_LOGW(TAG, "需要更新 USER 分区");
    }
    if((mask_res & PARTITION_ASR_FLAG_MASK) != 0) {
        ESP_LOGW(TAG, "需要更新 ASR 分区");
    }
    if((mask_res & PARTITION_DNN_FLAG_MASK) != 0) {
        ESP_LOGW(TAG, "需要更新 DNN 分区");
    }
    if((mask_res & PARTITION_VOICE_FLAG_MASK) != 0) {
        ESP_LOGW(TAG, "需要更新 VOICE 分区");
    }
    if((mask_res & PARTITION_USERFILE_FLAG_MASK) != 0) {
        ESP_LOGW(TAG, "需要更新 USER FILE 分区");
    }
    if(mask_res == 0) {
        ESP_LOGW(TAG, "已经是最新固件无需升级");
    }
    return mask_res;
}

bool chip_ota_updater_need_upgrade(uint32_t verify_res)
{
    if(verify_res == PARTITION_INVALID_FILE_MASK) {
        return false;
    } else if(verify_res == 0) {
        return false;
    }
    return true;
}

bool chip_ota_updater_send_partition_info(char* partition_name, uint32_t partition_addr, uint32_t partition_size, uint16_t partition_crc)
{
    uint8_t buff[32] = { 0 };
    uint32_t len = 0;

    buff[len++] = partition_addr&0xff;
    buff[len++] = (partition_addr>>8)&0xff;
    buff[len++] = (partition_addr>>16)&0xff;
    buff[len++] = (partition_addr>>24)&0xff;
    buff[len++] = partition_size&0xff;
    buff[len++] = (partition_size>>8)&0xff;
    buff[len++] = (partition_size>>16)&0xff;
    buff[len++] = (partition_size>>24)&0xff;
    buff[len++] = partition_crc&0xff;
    buff[len++] = (partition_crc>>8)&0xff;

    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_UPDATE_VERIFY_INFO, 0x01, buff, len);
    if(chip_ota_recv_cmd(0x00, MSG_TYPE_ACK, MSG_CMD_UPDATE_VERIFY_INFO, 0x00, NULL, 100) == false) {
        ESP_LOGE(TAG, "## 发送[%s]分区信息失败", partition_name);
        return false;
    }
    ESP_LOGI(TAG, "## 发送[%s]分区信息成功", partition_name);
    return true;
}

bool chip_ota_updater_send_partition_crc(char* partition_name, uint32_t partition_addr, uint32_t partition_size, uint16_t partition_crc)
{
    uint8_t buff[32] = { 0 };
    uint32_t len = 0;

    buff[len++] = partition_addr&0xff;
    buff[len++] = (partition_addr>>8)&0xff;
    buff[len++] = (partition_addr>>16)&0xff;
    buff[len++] = (partition_addr>>24)&0xff;
    buff[len++] = partition_size&0xff;
    buff[len++] = (partition_size>>8)&0xff;
    buff[len++] = (partition_size>>16)&0xff;
    buff[len++] = (partition_size>>24)&0xff;
    buff[len++] = partition_crc&0xff;
    buff[len++] = (partition_crc>>8)&0xff;
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_UPDATE_VERIFY, 0x01, buff, len);

    len = 0x01;
    memset(buff, 0x00, len);
    if(chip_ota_recv_cmd(len, MSG_TYPE_ACK, MSG_CMD_UPDATE_VERIFY, 0x00, buff, 100) == false) {
        ESP_LOGE(TAG, "## 验证[%s]分区超时", partition_name);
        return false;
    }
    if(buff[0] != 0x01) {
        ESP_LOGE(TAG, "## 验证[%s]分区失败", partition_name);
        return false;
    }
    ESP_LOGI(TAG, "## 验证[%s]分区成功", partition_name);
    return true;
}

bool chip_ota_updater_erase_partition(char* partition_name, uint32_t partition_addr, uint32_t partition_size, uint32_t erase_size)
{
    uint8_t buff[32] = { 0 };
    uint32_t len = 0;

    buff[len++] = partition_addr&0xff;
    buff[len++] = (partition_addr>>8)&0xff;
    buff[len++] = (partition_addr>>16)&0xff;
    buff[len++] = (partition_addr>>24)&0xff;
    buff[len++] = partition_size&0xff;
    buff[len++] = (partition_size>>8)&0xff;
    buff[len++] = (partition_size>>16)&0xff;
    buff[len++] = (partition_size>>24)&0xff;
    buff[len++] = erase_size&0xff;
    buff[len++] = (erase_size>>8)&0xff;
    buff[len++] = (erase_size>>16)&0xff;
    buff[len++] = (erase_size>>24)&0xff;

    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_UPDATE_ERA, 0x00, buff, len);
    if(chip_ota_recv_cmd(0x00, MSG_TYPE_ACK, MSG_CMD_UPDATE_ERA, 0x00, NULL, 100) == false) {
        ESP_LOGE(TAG, "## 擦除[%s]分区失败", partition_name);
        return false;
    }
    ESP_LOGI(TAG, "## 擦除[%s]分区成功", partition_name);
    return true;
}

bool chip_ota_updater_send_partition_data(char* partition_name, uint8_t* partition_data, uint32_t partition_data_size, uint32_t offset, uint32_t size)
{
    if(partition_data == NULL) {
        ESP_LOGE(TAG, "## [%s]数据有误", partition_name);
        return false;
    }
    uint32_t len = 4 + 4096;
    uint8_t* buff = (uint8_t*)malloc(len);
    if(buff == NULL) {
        ESP_LOGE(TAG, "## 申请[%s]数据空间内存失败", partition_name);
        return false;
    }

    memset(buff, 0xff, len); // defult fill 0xff
    buff[0] = offset&0xff; // offset
    buff[1] = (offset>>8)&0xff;
    buff[2] = (offset>>16)&0xff;
    buff[3] = (offset>>24)&0xff;

    int32_t remain = partition_data_size - offset;
    remain = remain < size ? remain : size;
    if(remain > 0) {
        len = 4 + remain;
        memcpy(buff+4, partition_data + offset, remain);
        s_ota_desc.calculate_crc = chip_ota_get_crc(s_ota_desc.calculate_crc, buff+4, remain);
    } else {
        len = 0;
    }

    chip_ota_send_cmd(MSG_TYPE_ACK, MSG_CMD_UPDATE_WRITE, 0x00, buff, len);
    free(buff);
    return true;
}

bool chip_ota_updater_send_partition(char* partition_name, uint8_t* partition_data, uint32_t partition_data_size)
{
    uint8_t buff[32] = { 0 };
    uint32_t len = 0, ticks = 0;
    uint8_t is_finish = 0, cmd = 0;
    ESP_LOGI(TAG, "## 等待设备请求[%s]分区数据", partition_name);
    s_ota_desc.calculate_crc = 0;

    while (1) {
        memset(buff, 0x00, sizeof(buff));
        if(is_finish) {
            len = 0x00; // 传输完成请求
            cmd = MSG_CMD_UPDATE_BLOCK_WRITE_DONE;
        } else {
            len = 0x08; // 传输数据包请求
            cmd = MSG_CMD_UPDATE_WRITE;
        }
        if(chip_ota_recv_cmd(len, MSG_TYPE_REQ, cmd, 0x00, buff, 500) == false) {
            // ESP_LOGE(TAG, "## 等待设备请求updater数据帧超时");
            ticks++;
        } else {
            ticks = 0;
            if(len == 0x00) {
                ESP_LOGI(TAG, "## 设备请求[%s]分区数据结束", partition_name);
                break;
            } else {
                uint32_t offset = buff[3]<<24 | buff[2]<<16 | buff[1]<<8 | buff[0];
                uint32_t size = buff[7]<<24 | buff[6]<<16 | buff[5]<<8 | buff[4];
                chip_ota_updater_send_partition_data(partition_name, partition_data, partition_data_size, offset, size);
                uint32_t proess = offset * 1.0 / partition_data_size * 100;
                if(offset + size >= partition_data_size) {
                    is_finish = 0x01;
                    proess = 100;
                }
                ESP_LOGI(TAG, "## 设备请求[%s]分区数据, 请求地址: 0x%X, 请求大小: %d, 进度: %d",partition_name, offset, size, proess);
            }
        }
        if(ticks >= 10) { // 10 * 200 = 2000ms
            ESP_LOGE(TAG, "## 等待设备请求[%s]分区数据超时", partition_name);
            return false;
        }
    }
    ESP_LOGI(TAG, "## 设备请求[%s]分区数据完成", partition_name);
    return true;
}

bool chip_ota_updater_update_partition_table_1(void)
{
    ESP_LOGI(TAG, "## 开始更新分区表-1");
    uint16_t partition_crc = 0;
    uint32_t partition_addr = PARTITION_TABLE1_START_ADDR;
    uint32_t partition_size = sizeof(partition_table_t);
    uint8_t* partition_data = (uint8_t*)&s_ota_desc.update_partition;
    if(chip_ota_updater_send_partition_info("分区表1", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    if(chip_ota_updater_erase_partition("分区表1", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
        return false;
    }
    if(chip_ota_updater_send_partition("分区表1", partition_data, partition_size) == false) {
        return false;
    }
    partition_crc = s_ota_desc.calculate_crc;
    if(chip_ota_updater_send_partition_crc("分区表1", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    ESP_LOGI(TAG, "## 成功更新分区表-1");
    return true;
}

bool chip_ota_updater_update_partition_table_2(void)
{
    ESP_LOGI(TAG, "## 开始更新分区表-2");
    uint16_t partition_crc = 0;
    uint32_t partition_addr = PARTITION_TABLE2_START_ADDR;
    uint32_t partition_size = sizeof(partition_table_t);
    uint8_t* partition_data = (uint8_t*)&s_ota_desc.update_partition;
    if(chip_ota_updater_send_partition_info("分区表2", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    if(chip_ota_updater_erase_partition("分区表2", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
        return false;
    }
    if(chip_ota_updater_send_partition("分区表2", partition_data, partition_size) == false) {
        return false;
    }
    partition_crc = s_ota_desc.calculate_crc;
    if(chip_ota_updater_send_partition_crc("分区表2", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    ESP_LOGI(TAG, "## 成功更新分区表-2");
    return true;
}

bool chip_ota_updater_update_partition_table(void)
{
    ESP_LOGI(TAG, "## 开始更新分区表");
    chip_ota_partition_info_print(&s_ota_desc.update_partition);
    if(chip_ota_updater_update_partition_table_1() == false) {
        return false;
    }
    if(chip_ota_updater_update_partition_table_2() == false) {
        return false;
    }
    ESP_LOGI(TAG, "## 成功更新分区表");
    return true;
}

bool chip_ota_updater_user1_partition(void)
{
    ESP_LOGI(TAG, "## 开始更新 USER1 分区");
    uint16_t partition_crc = 0;
    uint32_t partition_addr = s_ota_desc.frameware_partition.user_code1.address;
    uint32_t partition_size = s_ota_desc.frameware_partition.user_code1.size;
    uint8_t* partition_data = s_ota_desc.frameware + s_ota_desc.frameware_partition.user_code1.address;
    if(chip_ota_updater_send_partition_info("USER1 分区", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    if(chip_ota_updater_erase_partition("USER1 分区", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
        return false;
    }
    chip_ota_delay_ms(1000); // 等待擦除
    if(chip_ota_updater_send_partition("USER1 分区", partition_data, partition_size) == false) {
        return false;
    }
    partition_crc = s_ota_desc.calculate_crc;
    if(chip_ota_updater_send_partition_crc("USER1 分区", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    ESP_LOGI(TAG, "## 成功更新 USER1 分区");

    // 切换升级分区
    s_ota_desc.update_partition.user_code2 = s_ota_desc.frameware_partition.user_code2;
    s_ota_desc.update_partition.user_code1.status = 0xF0;
    s_ota_desc.update_partition.user_code2.status = 0xFC;
    s_ota_desc.update_partition.PartitionTableChecksum = chip_ota_partition_get_sum(&s_ota_desc.update_partition);
    return true;
}

bool chip_ota_updater_user2_partition(void)
{
    ESP_LOGI(TAG, "## 开始更新 USER2 分区");
    uint16_t partition_crc = 0;
    uint32_t partition_addr = s_ota_desc.frameware_partition.user_code2.address;
    uint32_t partition_size = s_ota_desc.frameware_partition.user_code2.size;
    uint8_t* partition_data = s_ota_desc.frameware + s_ota_desc.frameware_partition.user_code2.address;
    if(chip_ota_updater_send_partition_info("USER2 分区", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    if(chip_ota_updater_erase_partition("USER2 分区", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
        return false;
    }
    chip_ota_delay_ms(1000); // 等待擦除
    if(chip_ota_updater_send_partition("USER2 分区", partition_data, partition_size) == false) {
        return false;
    }
    partition_crc = s_ota_desc.calculate_crc;
    if(chip_ota_updater_send_partition_crc("USER2 分区", partition_addr, partition_size, partition_crc) == false) {
        return false;
    }
    ESP_LOGI(TAG, "## 成功更新 USER2 分区");

    // 切换升级分区
    s_ota_desc.update_partition.user_code1 = s_ota_desc.frameware_partition.user_code1;
    s_ota_desc.update_partition.user_code2.status = 0xF0;
    s_ota_desc.update_partition.user_code1.status = 0xFC;
    s_ota_desc.update_partition.PartitionTableChecksum = chip_ota_partition_get_sum(&s_ota_desc.update_partition);
    return true;
}

bool chip_ota_updater_user_partition(uint32_t verify_res)
{
    if(((verify_res & PARTITION_USER1_FLAG_MASK) != 0) || ((verify_res & PARTITION_USER2_FLAG_MASK) != 0)) {
        
        uint8_t user_partition = 1;
        if(verify_res & PARTITION_USER2_FLAG_MASK) {
            user_partition = 2;
        }

        if(user_partition == 1) {
            user_partition = 2;
            chip_ota_updater_user1_partition();
        } else {
            user_partition = 1;
            chip_ota_updater_user2_partition();
        }

        ESP_LOGI(TAG, "## 成功更新 USER%d 分区, 再次更新分区表", user_partition);
        chip_ota_partition_info_print(&s_ota_desc.update_partition);
        if(chip_ota_updater_update_partition_table() == false) {
            ESP_LOGE(TAG, "## 成功更新 USER%d 分区, 再次更新分区表失败", user_partition);
            return false;
        }
        ESP_LOGI(TAG, "## 成功更新 USER%d 分区, 再次更新分区表成功", user_partition);

        if(user_partition == 1) {
            chip_ota_updater_user1_partition();
        } else {
            chip_ota_updater_user2_partition();
        }
    }
    ESP_LOGI(TAG, "不需要更新 USER 分区");
    return true;
}

bool chip_ota_updater_asr_partition(uint32_t verify_res)
{
    if((verify_res & PARTITION_ASR_FLAG_MASK) != 0) {
        ESP_LOGI(TAG, "## 开始更新 ASR 分区");

        uint16_t partition_crc = 0;
        uint32_t partition_addr = s_ota_desc.frameware_partition.asr_cmd_model.address;
        uint32_t partition_size = s_ota_desc.frameware_partition.asr_cmd_model.size;
        uint8_t* partition_data = s_ota_desc.frameware + s_ota_desc.frameware_partition.asr_cmd_model.address;
        if(chip_ota_updater_send_partition_info("ASR 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        if(chip_ota_updater_erase_partition("ASR 分区", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
            return false;
        }
        chip_ota_delay_ms(500); // 等待擦除
        if(chip_ota_updater_send_partition("ASR 分区", partition_data, partition_size) == false) {
            return false;
        }
        partition_crc = s_ota_desc.calculate_crc;
        if(chip_ota_updater_send_partition_crc("ASR 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        ESP_LOGI(TAG, "## 成功更新 ASR 分区");
    }
    ESP_LOGI(TAG, "不需要更新 ASR 分区");
    return true;
}

bool chip_ota_updater_dnn_partition(uint32_t verify_res)
{
    if((verify_res & PARTITION_DNN_FLAG_MASK) != 0) {
        ESP_LOGI(TAG, "## 开始更新 DNN 分区");
        
        uint16_t partition_crc = 0;
        uint32_t partition_addr = s_ota_desc.frameware_partition.dnn_model.address;
        uint32_t partition_size = s_ota_desc.frameware_partition.dnn_model.size;
        uint8_t* partition_data = s_ota_desc.frameware + s_ota_desc.frameware_partition.dnn_model.address;
        if(chip_ota_updater_send_partition_info("DNN 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        if(chip_ota_updater_erase_partition("DNN 分区", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
            return false;
        }
        chip_ota_delay_ms(2000); // 等待擦除
        if(chip_ota_updater_send_partition("DNN 分区", partition_data, partition_size) == false) {
            return false;
        }
        partition_crc = s_ota_desc.calculate_crc;
        if(chip_ota_updater_send_partition_crc("DNN 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        ESP_LOGI(TAG, "## 成功更新 DNN 分区");
    }
    ESP_LOGI(TAG, "不需要更新 DNN 分区");
    return true;
}

bool chip_ota_updater_voice_partition(uint32_t verify_res)
{
    if((verify_res & PARTITION_VOICE_FLAG_MASK) != 0) {
        ESP_LOGI(TAG, "## 开始更新 VOICE 分区");
        
        uint16_t partition_crc = 0;
        uint32_t partition_addr = s_ota_desc.frameware_partition.voice.address;
        uint32_t partition_size = s_ota_desc.frameware_partition.voice.size;
        uint8_t* partition_data = s_ota_desc.frameware + s_ota_desc.frameware_partition.voice.address;
        if(chip_ota_updater_send_partition_info("VOICE 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        if(chip_ota_updater_erase_partition("VOICE 分区", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
            return false;
        }
        chip_ota_delay_ms(300); // 等待擦除
        if(chip_ota_updater_send_partition("VOICE 分区", partition_data, partition_size) == false) {
            return false;
        }
        partition_crc = s_ota_desc.calculate_crc;
        if(chip_ota_updater_send_partition_crc("VOICE 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        ESP_LOGI(TAG, "## 成功更新 VOICE 分区");
    }
    ESP_LOGI(TAG, "不需要更新 VOICE 分区");
    return true;
}


bool chip_ota_updater_user_file_partition(uint32_t verify_res)
{
    if((verify_res & PARTITION_USERFILE_FLAG_MASK) != 0) {
        ESP_LOGI(TAG, "## 开始更新 USER FILE 分区");
        
        uint16_t partition_crc = 0;
        uint32_t partition_addr = s_ota_desc.frameware_partition.user_file.address;
        uint32_t partition_size = s_ota_desc.frameware_partition.user_file.size;
        uint8_t* partition_data = s_ota_desc.frameware + s_ota_desc.frameware_partition.user_file.address;
        if(chip_ota_updater_send_partition_info("USER FILE 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        if(chip_ota_updater_erase_partition("USER FILE 分区", partition_addr, partition_size, ERASE_BLOCK_SIZE) == false) {
            return false;
        }
        chip_ota_delay_ms(300); // 等待擦除
        if(chip_ota_updater_send_partition("USER FILE 分区", partition_data, partition_size) == false) {
            return false;
        }
        partition_crc = s_ota_desc.calculate_crc;
        if(chip_ota_updater_send_partition_crc("USER FILE 分区", partition_addr, partition_size, partition_crc) == false) {
            return false;
        }
        ESP_LOGI(TAG, "## 成功更新 USER FILE 分区");
    }
    ESP_LOGI(TAG, "不需要更新 USER FILE 分区");
    return true;
}

bool chip_ota_updater_exit_upgrade(void)
{
    ESP_LOGI(TAG, "退出升级, 重启设备");
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_SYS_RST, 0x00, NULL, 0);
    chip_ota_delay_ms(100);
    chip_ota_send_cmd(MSG_TYPE_CMD, MSG_CMD_SYS_RST, 0x00, NULL, 0);
    return true;
}




