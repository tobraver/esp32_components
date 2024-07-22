# 项目配置

1. 选择唤醒模型

2. 选择识别模型 wn7

3. 配置分区表

```
model,        data, spiffs,        ,    4152K,
```


# 项目说明

```
AUDIO_REC_WAKEUP_TIMEOUT: 唤醒后, 多长时间未讲话, 进入休眠状态。

AUDIO_REC_VAD_SPEAK_TIME: VAD检测说话, 持续时间; 真实人声, 要比视频播放人声效果好。

AUDIO_REC_VAD_SILENCE_TIME: 检测静音, 持续时间; 取决于人声说话停顿时间。

```


# 注意事项

1. 喂狗报警告, 打印CPU使用率, 优化CPU占用率

``` c
#include "audio_sys.h"

audio_sys_get_real_time_stats();
```

2. 内存不足, 打印各个内存剩余大小

``` c
#include "esp_system.h"

ESP_LOGI("", "[APP] Free memory: %ld bytes,esp_get_free_internal_heap_size :%ld bytes,esp_get_minimum_free_heap_size:%ld bytes", esp_get_free_heap_size(),esp_get_free_internal_heap_size(),esp_get_minimum_free_heap_size());
```

3. 提高设备性能, 提高CPU主频到240MHz, 提高PSRAM主频到120MHz

[参考配置: 如何提高 LCD 的显示帧率](https://docs.espressif.com/projects/esp-faq/zh_CN/latest/software-framework/peripherals/lcd.html#id2)


