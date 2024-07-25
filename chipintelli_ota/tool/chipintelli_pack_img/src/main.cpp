#include <iostream>
#include <fstream>
#include <vector>
#include <string.h>
#include "inih/INIReader.h"

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


uint16_t ota_partition_get_sum(partition_table_t *partition)
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

void partition_info_print(partition_table_t* partition)
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

uint16_t ota_get_crc(uint16_t crc, uint8_t *buf, uint32_t len)
{
    uint32_t counter;
    for (counter = 0; counter < len; counter++)
    {
        unsigned char t = *(unsigned char *)buf++;
        crc = (crc << 8) ^ crc16tab_ccitt[((crc >> 8) ^ t) & 0x00FF];
    }
    return crc;
}

std::vector<char> get_image(std::string file_name)
{
    std::ifstream file;
    file.open(file_name, std::ios::binary);
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

void save_image(std::string file_name, std::vector<char> &buffer)
{
    std::ofstream file;
    file.open(file_name, std::ios::binary);
    file.write(buffer.data(), buffer.size());
    file.close();
}

bool pack_image(const char* tag, std::vector<char> &buffer, uint32_t offset, uint8_t* src, uint32_t size)
{
    if(buffer.size() < offset + size) {
        printf("## partition [%s] pack is overflow\n", tag);
        return false;
    }
    uint8_t* p_buff = (uint8_t*)buffer.data();
    for(uint32_t i=0; i<size; i++) {
        p_buff[offset + i] = src[i];
    }
    printf("-- partition [%s] is pack\n", tag);
    return true;
}

bool load_image(const char* tag, const char* file, std::vector<char>& img)
{
    img = get_image(file);
    if(img.size()) {
        printf("-- load partiton [%s] success, size: %d bytes\n", tag, img.size());
        return true;
    }
    printf("## load partiton [%s] fail\n", tag);
    return false;
}

bool check_conf_size(const char* tag, int32_t conf_size, int32_t file_size)
{
    if(conf_size < 0) {
        printf("## [%s] size is invalid\n", tag);
        return false;
    }
    if(conf_size % 0x1000) {
        printf("## [%s] size is not (n * 4096)\n", tag);
        return false;
    }
    if(conf_size < file_size) {
        printf("## [%s] config size [%d] is small than [%s] image size[%d]\n", tag, conf_size, tag, file_size);
        return false;
    }
    return true;
}

/**
 * @brief 处理镜像
 */
int main(int argc, char const *argv[])
{
    /** 加载镜像文件 **/
    std::vector<char> bootloader_img, user_code_img, asr_img, dnn_img, voice_img, user_file_img;
    if(load_image("bootloader", "partition/bootloader.bin", bootloader_img) == false) {
        return 1;
    }
    if(load_image("user_code", "partition/user_code.bin", user_code_img) == false) {
        return 1;
    }
    if(load_image("asr", "partition/asr.bin", asr_img) == false) {
        return 1;
    }
    if(load_image("dnn", "partition/dnn.bin", dnn_img) == false) {
        return 1;
    }
    if(load_image("voice", "partition/voice.bin", voice_img) == false) {
        return 1;
    }
    if(load_image("user_file", "partition/user_file.bin", user_file_img) == false) {
        return 1;
    }

    /** 加载配置文件 **/
    INIReader reader("config/config.ini");
    if (reader.ParseError() < 0) {
        std::cout << "## load config.ini failed\n";
        return 1;
    }

    partition_table_t partition;
    memset(&partition, 0x00, sizeof(partition_table_t));
    std::string soft_name = reader.GetString("PACKAGE", "soft_name", "");
    std::string hard_name = reader.GetString("PACKAGE", "hard_name", "");

    if(soft_name.empty() || hard_name.empty()) {
        printf("## software name or hardware name is empty\n");
        return 1;
    }
    if(soft_name.length() > sizeof(partition.SWName) - 1) {
        printf("## software name is too long, max %d\n", sizeof(partition.SWName) - 1);
        return 1;
    }
    if(hard_name.length() > sizeof(partition.HWName) - 1) {
        printf("## hardware name is too long, max %d\n", sizeof(partition.HWName) - 1);
        return 1;
    }

    uint8_t soft_version_1 = reader.GetInteger("PACKAGE", "soft_version_1", 0);
    uint8_t soft_version_2 = reader.GetInteger("PACKAGE", "soft_version_2", 0);
    uint8_t soft_version_3 = reader.GetInteger("PACKAGE", "soft_version_3", 0);
    uint8_t hard_version_1 = reader.GetInteger("PACKAGE", "hard_version_A", 0);
    uint8_t hard_version_2 = reader.GetInteger("PACKAGE", "hard_version_B", 0);
    uint8_t hard_version_3 = reader.GetInteger("PACKAGE", "hard_version_C", 0);
    std::string soft_version = std::to_string(soft_version_1) + "." + std::to_string(soft_version_2) + "." + std::to_string(soft_version_3);
    std::string hard_version = std::to_string(hard_version_1) + "." + std::to_string(hard_version_2) + "." + std::to_string(hard_version_3);
    printf("-- soft version: %s, hard vesion: %s\n", soft_version.c_str(), hard_version.c_str());

    int32_t default_version = 100;
    int32_t user_code1_offset = 0xA000;
    int32_t user_code1_size = 0x22000;
    int32_t user_code1_version = reader.GetInteger("PACKAGE", "user_version", default_version);
    if(check_conf_size("user_code1", user_code1_size, user_code_img.size()) == false) {
        return 1;
    }

    int32_t user_code2_offset = user_code1_offset + user_code1_size;
    int32_t user_code2_size = user_code1_size;
    int32_t user_code2_version = user_code1_version;
    if(check_conf_size("user_code2", user_code2_size, user_code_img.size()) == false) {
        return 1;
    }

    int32_t asr_offset = reader.GetInteger("PACKAGE", "command_addr", -1);
    if(asr_offset < 0 || asr_offset < (user_code2_offset + user_code2_size)) {
        printf("## asr offset is invalid, config offset: 0x%X, min offset: 0x%X\n", asr_offset, user_code2_offset + user_code2_size);
        return 1;
    }
    int32_t asr_size = reader.GetInteger("PACKAGE", "command_size", -1);
    if(check_conf_size("asr", asr_size, asr_img.size()) == false) {
        return 1;
    }
    int32_t asr_version = reader.GetInteger("PACKAGE", "command_version", default_version);

    int32_t dnn_offset = reader.GetInteger("PACKAGE", "module_addr", -1);
    if(dnn_offset < 0 || dnn_offset < (asr_offset + asr_size)) {
        printf("## dnn offset is invalid, config offset: 0x%X, min offset: 0x%X\n", dnn_offset, asr_offset + asr_size);
        return 1;
    }
    int32_t dnn_size = reader.GetInteger("PACKAGE", "module_size", -1);
    if(check_conf_size("dnn", dnn_size, dnn_img.size()) == false) {
        return 1;
    }
    int32_t dnn_version = reader.GetInteger("PACKAGE", "module_version", default_version);

    int32_t voice_offset = reader.GetInteger("PACKAGE", "voice_addr", -1);
    if(voice_offset < 0 || voice_offset < (dnn_offset + dnn_size)) {
        printf("## voice offset is invalid, config offset: 0x%X, min offset: 0x%X\n", voice_offset, dnn_offset + dnn_size);
        return 1;
    }
    int32_t voice_size = reader.GetInteger("PACKAGE", "voice_size", -1);
    if(check_conf_size("voice", voice_size, voice_img.size()) == false) {
        return 1;
    }
    int32_t voice_version = reader.GetInteger("PACKAGE", "voice_version", default_version);

    int32_t user_file_offset = reader.GetInteger("PACKAGE", "user_file_addr", -1);
    if(user_file_offset < 0 || user_file_offset < (voice_offset + voice_size)) {
        printf("## user_file offset is invalid, config offset: 0x%X, min offset: 0x%X\n", user_file_offset, voice_offset + voice_size);
        return 1;
    }
    int32_t user_file_size = reader.GetInteger("PACKAGE", "user_file_size", -1);
    if(check_conf_size("user_file", user_file_size, user_file_img.size()) == false) {
        return 1;
    }
    int32_t user_file_version = reader.GetInteger("PACKAGE", "user_file_version", default_version);

    printf("-- user_code1 offset = 0x%X, version: %d, conf size = 0x%X, img size: 0x%X\n", user_code1_offset, user_code1_version, user_code1_size, user_code_img.size());
    printf("-- user_code2 offset = 0x%X, version: %d, conf size = 0x%X, img size: 0x%X\n", user_code2_offset, user_code2_version, user_code2_size, user_code_img.size());
    printf("-- asr offset = 0x%X, version: %d, conf size = 0x%X, img size: 0x%X\n", asr_offset, asr_version, asr_size, asr_img.size());
    printf("-- dnn offset = 0x%X, version: %d, conf size = 0x%X, img size: 0x%X\n", dnn_offset, dnn_version, dnn_size, dnn_img.size());
    printf("-- voice offset = 0x%X, version: %d, conf size = 0x%X, img size: 0x%X\n", voice_offset, voice_version, voice_size, voice_img.size());
    printf("-- user_file offset = 0x%X, version: %d, conf size = 0x%X, img size: 0x%X\n", user_file_offset, user_file_version, user_file_size, user_file_img.size());

    /** 更新分区表信息 **/
    partition.ManufacturerID = 100;
    partition.ProductID[0] = 100;
    memcpy((char*)&partition.HWName, hard_name.c_str(), hard_name.length());
    partition.HWVersion = (hard_version_1<<16) | (hard_version_2<<8) | hard_version_3;
    memcpy((char*)&partition.SWName, soft_name.c_str(), soft_name.length());
    partition.SWVersion = (soft_version_1<<16) | (soft_version_2<<8) | soft_version_3;
    partition.BootLoaderVersion = 256;
    memcpy((char*)&partition.ChipName, "CI130*", 6);
    partition.FirmwareFormatVer = 1;
    partition.reserve[0] = 80; partition.reserve[1] = 51; partition.reserve[2] = 57; partition.reserve[3] = 53;

    partition.user_code1.version = user_code1_version;
    partition.user_code1.address = user_code1_offset;
    partition.user_code1.size = user_code_img.size(); // remain 0x22000
    partition.user_code1.status = 0xF0;
    partition.user_code1.crc = ota_get_crc(0, (uint8_t*)user_code_img.data(), user_code_img.size());

    partition.user_code2.version = user_code2_version;
    partition.user_code2.address = user_code2_offset;
    partition.user_code2.size = user_code_img.size();
    partition.user_code2.status = 0xF0;
    partition.user_code2.crc = ota_get_crc(0, (uint8_t*)user_code_img.data(), user_code_img.size());

    partition.asr_cmd_model.version = asr_version;
    partition.asr_cmd_model.address = asr_offset;
    partition.asr_cmd_model.size = asr_img.size(); // 0x3000
    partition.asr_cmd_model.status = 0x00;
    partition.asr_cmd_model.crc = ota_get_crc(0, (uint8_t*)asr_img.data(), asr_img.size());

    partition.dnn_model.version = dnn_version;
    partition.dnn_model.address = dnn_offset;
    partition.dnn_model.size = dnn_img.size();
    partition.dnn_model.status = 0x00;
    partition.dnn_model.crc = ota_get_crc(0, (uint8_t*)dnn_img.data(), dnn_img.size());

    partition.voice.version = voice_version;
    partition.voice.address = voice_offset;
    partition.voice.size = voice_img.size();
    partition.voice.status = 0x00;
    partition.voice.crc = ota_get_crc(0, (uint8_t*)voice_img.data(), voice_img.size());

    partition.user_file.version = user_file_version;
    partition.user_file.address = user_file_offset;
    partition.user_file.size = user_file_img.size();
    partition.user_file.status = 0x00;
    partition.user_file.crc = ota_get_crc(0, (uint8_t*)user_file_img.data(), user_file_img.size());

    partition.ConsumerDataStartAddr = 0x3FC000;
    partition.ConsumerDataSize = 0x4000;
    partition.PartitionTableChecksum = ota_partition_get_sum(&partition);

    partition_info_print(&partition); /*! 打印分区表详情 */

    std::string pack_img_name = soft_name + "_" + soft_version + ".bin";
    uint32_t pack_img_size = partition.user_file.address + user_file_img.size();
    std::vector<char> pack_img(pack_img_size);
    memset((uint8_t*)pack_img.data(), 0xff, pack_img_size); /*! default vaule 0xff */

    printf("-- pack img name = %s, size = %d\n", pack_img_name.c_str(), pack_img_size);

    if(pack_image("bootloader", pack_img, 0, (uint8_t*)bootloader_img.data(), bootloader_img.size()) == false) {
        return false;
    }
    if(pack_image("partition_table1", pack_img, 0x6000, (uint8_t*)&partition, sizeof(partition_table_t)) == false) {
        return false;
    }
    if(pack_image("partition_table2", pack_img, 0x8000, (uint8_t*)&partition, sizeof(partition_table_t)) == false) {
        return false;
    }
    if(pack_image("user_code1", pack_img, user_code1_offset, (uint8_t*)user_code_img.data(), user_code_img.size()) == false) {
        return false;
    }
    if(pack_image("user_code2", pack_img, user_code2_offset, (uint8_t*)user_code_img.data(), user_code_img.size()) == false) {
        return false;
    }
    if(pack_image("asr", pack_img, asr_offset, (uint8_t*)asr_img.data(), asr_img.size()) == false) {
        return false;
    }
    if(pack_image("dnn", pack_img, dnn_offset, (uint8_t*)dnn_img.data(), dnn_img.size()) == false) {
        return false;
    }
    if(pack_image("voice", pack_img, voice_offset, (uint8_t*)voice_img.data(), voice_img.size()) == false) {
        return false;
    }
    if(pack_image("user_file", pack_img, user_file_offset, (uint8_t*)user_file_img.data(), user_file_img.size()) == false) {
        return false;
    }

    save_image(pack_img_name, pack_img);
    printf("-- pack image success!\n");
    return 0;
}



