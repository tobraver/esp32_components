#include "http_sdmmc.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_wifi.h"
#include "http_stream.h"
#include "fatfs_stream.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sys/stat.h"

static const char *TAG = "http_sdmmc";

typedef struct {
    uint8_t is_init;
    uint8_t sdmmc_ready;
    sdmmc_card_t* sdmmc_card;
    esp_periph_set_handle_t periph_set;
} http_sdmmc_desc_t;

static http_sdmmc_desc_t s_http_sdmmc;

static void _sdmmc_info(const sdmmc_card_t* card)
{
    bool print_scr = false;
    bool print_csd = false;
    const char* type;
    printf("## sdmmc name: %s\n", card->cid.name);
    if (card->is_sdio) {
        type = "SDIO";
        print_scr = true;
        print_csd = true;
    } else if (card->is_mmc) {
        type = "MMC";
        print_csd = true;
    } else {
        type = (card->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC";
    }
    printf("## sdmmc type: %s\n", type);
    if (card->max_freq_khz < 1000) {
        printf("## sdmmc speed: %d kHz\n", card->max_freq_khz);
    } else {
        printf("## sdmmc speed: %d MHz%s\n", card->max_freq_khz / 1000,
                card->is_ddr ? ", DDR" : "");
    }
    printf("## sdmmc capacity: %lluMB\n", ((uint64_t) card->csd.capacity) * card->csd.sector_size / (1024 * 1024));

    if (print_csd) {
        printf("## sdmmc csd: ver=%d, sector_size=%d, capacity=%d read_bl_len=%d\n",
                card->csd.csd_ver,
                card->csd.sector_size, card->csd.capacity, card->csd.read_block_len);
    }
    if (print_scr) {
        printf("## sdmmc scr: sd_spec=%d, bus_width=%d\n", card->scr.sd_spec, card->scr.bus_width);
    }
}

static esp_err_t _sdmmc_mount(void)
{
    esp_err_t ret = ESP_OK;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef HTTP_SDMMC_FORMAT_ENABLE
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // HTTP_SDMMC_FORMAT_ENABLE
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char* mount_point = HTTP_SDMMC_MOUNT_PATH;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = SD_MODE_1_LINE;
    slot_config.clk = HTTP_SDMMC_PIN_CLK;
    slot_config.cmd = HTTP_SDMMC_PIN_CMD;
    slot_config.d0 = HTTP_SDMMC_PIN_D0;

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    _sdmmc_info(card);
    s_http_sdmmc.sdmmc_card = card;
    return ret;
}

/**
 * @brief http sdmmc mount
 * 
 * @return esp_err_t 
 *  @retval ESP_OK : sd card mount success
 *  @retval ESP_FAIL : sd card mount failed
 */
esp_err_t http_sdmmc_init(void)
{
    if(s_http_sdmmc.is_init) {
        ESP_LOGI(TAG, "sdmmc already init");
        return ESP_OK;
    }

    int retry_time = HTTP_SDMMC_MOUNT_RETRY;
    bool mount_flag = false;
    while (retry_time --) {
        if (_sdmmc_mount() == ESP_OK) {
            mount_flag = true;
            break;
        } else {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
    s_http_sdmmc.is_init = true;
    s_http_sdmmc.sdmmc_ready = mount_flag ? 1 : 0;
    if (mount_flag == false) {
        ESP_LOGE(TAG, "sdmmc mount failed");
        return ESP_FAIL;
    }    
    ESP_LOGI(TAG, "sdmmc mount success");
    return ESP_OK; 
}

/**
 * @brief http sdmmc deinit
 * 
 * @return esp_err_t 
 *  @retval ESP_OK : sdmmc deinit success
 *  @retval ESP_FAIL : sdmmc deinit failed
 */
esp_err_t http_sdmmc_deinit(void)
{
    if(s_http_sdmmc.is_init == 0) {
        ESP_LOGW(TAG, "http sdmmc already deinit");
        return ESP_OK;
    }
    
    if(s_http_sdmmc.sdmmc_card == NULL) {
        ESP_LOGW(TAG, "sdmmc handle is null");
        return ESP_OK;
    }

    esp_vfs_fat_sdcard_unmount(HTTP_SDMMC_MOUNT_PATH, s_http_sdmmc.sdmmc_card);
    ESP_LOGI(TAG, "sdmmc unmount success");

    s_http_sdmmc.is_init = 0;
    s_http_sdmmc.sdmmc_ready = 0;
    return ESP_OK;
}

/**
 * @brief http sdmmc ready
 * 
 * @return esp_err_t 
 *  @retval ESP_OK : sdmmc is mount
 *  @retval ESP_FAIL : sdmmc is not mount
 */
esp_err_t http_sdmmc_ready(void)
{
    return s_http_sdmmc.sdmmc_ready ? ESP_OK : ESP_FAIL;
}

/**
 * @brief download http uri to sdmmc
 * 
 * @param url : http uri
 * @param file : sdmmc file path [@e.g. /sdcard/music.mp3]
 * @return esp_err_t 
 *  @retval ESP_OK : download success
 *  @retval ESP_FAIL : download failed
 * @note 
 *  1. file path must be start with "/sdcard/"
 *  2. file will be create whether success or failed
 *  3. http connect sever timeout is 30s
 *  4. http download file timeout is 2min
 */
esp_err_t http_sdmmc_download(char* url, char* file)
{
    audio_pipeline_handle_t pipeline = NULL;
    audio_element_handle_t fatfs_writer = NULL, http_reader = NULL;

    if(url == NULL || file == NULL) {
        ESP_LOGE(TAG, "url or file is null");
        return ESP_FAIL;
    }

    char* prefix = HTTP_SDMMC_MOUNT_PATH;
    char* substr = strstr(file, prefix);
    if(substr == NULL || substr != file) {
        ESP_LOGE(TAG, "file prefix error, prefix must is %s", prefix);
        return ESP_FAIL;
    }

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if(pipeline == NULL) {
        ESP_LOGE(TAG, "create pipeline failed");
        return ESP_FAIL;
    }

    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_reader = http_stream_init(&http_cfg);
    if(http_reader == NULL) {
        ESP_LOGE(TAG, "create http stream failed");
        goto _create_failed;
    }

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    fatfs_writer = fatfs_stream_init(&fatfs_cfg);
    if(fatfs_writer == NULL) {
        ESP_LOGE(TAG, "create fatfs stream failed");
        goto _create_failed;
    }

    audio_pipeline_register(pipeline, http_reader, "http_reader");
    audio_pipeline_register(pipeline, fatfs_writer, "fatfs_writer");

    const char *link_tag[2] = {"http_reader", "fatfs_writer"};
    audio_pipeline_link(pipeline, &link_tag[0], 2);

    audio_element_set_uri(http_reader, url);
    audio_element_set_uri(fatfs_writer, file);

    audio_pipeline_run(pipeline);

    ESP_LOGI(TAG, "waiting for download....");
    uint32_t download_timeout = 2*60; // [seconds]
    uint32_t download_flag = false;
    uint32_t download_file_size = 0, sdmmc_file_size = 0;
    audio_element_state_t el_state = AEL_STATE_RUNNING;
    audio_element_info_t el_info = { 0 };
    while (download_timeout--) {
        el_state = audio_element_get_state(http_reader);
        if(el_state == AEL_STATE_ERROR) {
            ESP_LOGE(TAG, "http download error");
            break;
        } else if(el_state == AEL_STATE_RUNNING) {
            ESP_LOGI(TAG, "downloading url %s", url);
            if(download_file_size == 0) {
                audio_element_getinfo(http_reader, &el_info);
                download_file_size = el_info.total_bytes;
            }
        }

        el_state = audio_element_get_state(fatfs_writer);
        if(el_state == AEL_STATE_ERROR) {
            ESP_LOGE(TAG, "fatfs write error");
            break;
        } else if(el_state == AEL_STATE_FINISHED) {
            ESP_LOGI(TAG, "fatfs write finish");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, http_reader);
    audio_pipeline_unregister(pipeline, fatfs_writer);

    audio_pipeline_deinit(pipeline);
    audio_element_deinit(fatfs_writer);
    audio_element_deinit(http_reader);

    struct stat file_stat = { 0 };
    stat(file, &file_stat);
    sdmmc_file_size = file_stat.st_size;
    if(sdmmc_file_size && sdmmc_file_size == download_file_size) {
        download_flag = true;
    } else {
        ESP_LOGE(TAG, "download file is incomplete, sdmmc file size %u, download file size %u", sdmmc_file_size, download_file_size);
    }

    ESP_LOGI(TAG, "download finish....");
    return download_flag ? ESP_OK : ESP_FAIL;

_create_failed:
    if(fatfs_writer) {
        audio_element_deinit(fatfs_writer);
    }
    if(http_reader) {
        audio_element_deinit(http_reader);
    }
    if(pipeline) {
        audio_pipeline_deinit(pipeline);
    }
    return ESP_FAIL;
}



