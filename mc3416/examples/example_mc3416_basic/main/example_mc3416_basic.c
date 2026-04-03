#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "mc3416.h"

#define I2C_MASTER_SDA_IO       7               /*!< GPIO number for I2C master data  */
#define I2C_MASTER_SCL_IO       8               /*!< GPIO number for I2C master clock */
#define I2C_MASTER_NUM          I2C_NUM_0       /*!< I2C port number for master */
#define I2C_MASTER_CLK_SPEED    100000          /*!< I2C master clock frequency (Hz) */

static const char *TAG = "MC3416 example";
static mc3416_handle_t sensor;

static void mc3416_task(void *pvParameters)
{
    int16_t x, y, z;
    while (1) {
        x = y = z = 0;
        mc3416_get_acceleration(sensor, &x, &y, &z);
        ESP_LOGI(TAG, "x=%d, y=%d, z=%d", x, y, z);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

static void i2c_bus_init(void)
{
    esp_err_t err;

    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = (gpio_num_t)I2C_MASTER_SDA_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_io_num       = (gpio_num_t)I2C_MASTER_SCL_IO,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_CLK_SPEED,
    };

    err = i2c_param_config(I2C_MASTER_NUM, &conf);
    assert(ESP_OK == err);
    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed: %s", esp_err_to_name(err));
    }
}

static void mc3416_sensor_init(void)
{
    i2c_bus_init();
    sensor = mc3416_create(I2C_MASTER_NUM, MC3416_I2C_ADDR_0);
    if (sensor == NULL) {
        ESP_LOGE(TAG, "Sensor handle creation failed");
        return;
    }
    ESP_LOGI(TAG, "Sensor handle created");
}

static void mc3416_sensor_start(void)
{
    uint8_t reg_val;

    // Enter STANDBY before changing configuration
    mc3416_set_mode(sensor, MC3416_MODE_STANDBY);

    // Configure range: ±2g
    mc3416_set_range(sensor, MC3416_RANGE_2G);
    mc3416_get_range(sensor, &reg_val);
    ESP_LOGI(TAG, "Range register: 0x%02x", reg_val);

    // Configure ODR: 128 Hz
    mc3416_set_sample_rate(sensor, MC3416_SAMPLE_128Hz);
    mc3416_get_sample_rate(sensor, &reg_val);
    ESP_LOGI(TAG, "Sample rate register: 0x%02x", reg_val);

    // Switch to WAKE for continuous sampling
    mc3416_set_mode(sensor, MC3416_MODE_WAKE);
    mc3416_get_mode(sensor, &reg_val);
    ESP_LOGI(TAG, "Mode: %s", (reg_val == MC3416_MODE_WAKE) ? "WAKE" : "STANDBY");

    xTaskCreatePinnedToCore(&mc3416_task, "mc3416_task", 2048, NULL, 5, NULL, 1);
}

void app_main(void)
{
    uint8_t chip_id;
    esp_err_t err;

    mc3416_sensor_init();

    err = mc3416_get_chip_id(sensor, &chip_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Chip ID: 0x%02x", chip_id);
    if (chip_id == MC3416_DEFAULT_CHIP_ID) {
        ESP_LOGI(TAG, "Chip ID is correct");
        mc3416_sensor_start();
    } else {
        ESP_LOGE(TAG, "Chip ID mismatch (expected 0x%02x, got 0x%02x)",
                 MC3416_DEFAULT_CHIP_ID, chip_id);
    }
}
