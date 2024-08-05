# 项目说明

基于ESP32支持多分区升级的HTTP OTA组件。支持固件、模型、音频、自定义等升级。


# 项目配置

1. 分区表配置

如果升级过程中，没有配置升级内容的分区表，会报错。

设备固件升级需要分区表：`otadata ota_0 ota_1`

模型升级需要分区表：`model`

音频升级需要分区表：`flash_tone`

```csv
nvs,          data, nvs,     0x9000,    0x4000,
otadata,      data, ota,     0xd000,    0x2000,
phy_init,     data, phy,     0xf000,    0x1000,
ota_0,        app,  ota_0,         ,    2624K,
ota_1,        app,  ota_1,         ,    2624K,
model,        data, spiffs,        ,    4152K,
flash_tone,   data, 0x27,          ,    200K,
```

2. 使能HTTP OTA

开启宏 `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`，支持HTTP OTA升级。

# 升级类型说明

支持固件、模型、音频、自定义等升级。

```c
typedef enum {
    HTTP_OTA_UPDATE_TYPE_MODEL, // 模型升级
    HTTP_OTA_UPDATE_TYPE_MUSIC, // 音频升级
    HTTP_OTA_UPDATE_TYPE_USER_1, // 自定义升级1
    HTTP_OTA_UPDATE_TYPE_USER_2, // 自定义升级2
    HTTP_OTA_UPDATE_TYPE_USER_3, // 自定义升级3
    HTTP_OTA_UPDATE_TYPE_FIRMWARE, // 固件升级
    HTTP_OTA_UPDATE_TYPE_MAX,
} http_ota_update_type_t;

```

# 升级说明

1. 升级过程中，如果有分区升级失败，则会终止整个升级流程。

2. 升级固件程序成功后，会自动重启设备，其他类型的升级，不会重启设备。

3. 如果是HTTPS升级, 需要配置HTTPS证书。
