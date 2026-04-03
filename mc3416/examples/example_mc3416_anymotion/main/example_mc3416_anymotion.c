#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "mc3416.h"

#define I2C_MASTER_SDA_IO       7               /*!< GPIO number for I2C master data  */
#define I2C_MASTER_SCL_IO       8               /*!< GPIO number for I2C master clock */
#define I2C_MASTER_NUM          I2C_NUM_0       /*!< I2C port number for master */
#define I2C_MASTER_CLK_SPEED    100000          /*!< I2C master clock frequency (Hz) */

#define GPIO_INTN               4               /*!< GPIO connected to MC3416 INTN pin */
#define ESP_INTR_FLAG_DEFAULT   0

static const char *TAG = "MC3416 anymotion";
static mc3416_handle_t sensor;
static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void intr_task(void *arg)
{
    uint32_t io_num;
    uint8_t intr_status;

    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGI(TAG, "[INTN] Interrupt on GPIO[%"PRIu32"] detected", io_num);

            // Read and print interrupt status; reading clears the flags
            if (mc3416_get_interrupt_status_reg(sensor, &intr_status) == ESP_OK) {
                ESP_LOGI(TAG, "Interrupt status: 0x%02x%s%s%s%s%s%s",
                         intr_status,
                         (intr_status & (1 << 7)) ? " ACQ_INT"     : "",
                         (intr_status & (1 << 4)) ? " TILT_35_INT" : "",
                         (intr_status & (1 << 3)) ? " SHAKE_INT"   : "",
                         (intr_status & (1 << 2)) ? " ANYM_INT"    : "",
                         (intr_status & (1 << 1)) ? " FLIP_INT"    : "",
                         (intr_status & (1 << 0)) ? " TILT_INT"    : "");
            }
        }
    }
}

static void gpio_intr_init(void)
{
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_NEGEDGE,          // INTN is active-low by default
        .pin_bit_mask = (1ULL << GPIO_INTN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(intr_task, "intr_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INTN, gpio_isr_handler, (void *)GPIO_INTN);
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
    }
}

static void mc3416_motion_config(void)
{
    uint16_t threshold;
    uint8_t reg_val;

    // Configure AnyMotion threshold (15-bit value, units depend on range/sensitivity)
    mc3416_write_anymotion_threshold(sensor, 400);
    mc3416_read_anymotion_threshold(sensor, &threshold);
    ESP_LOGI(TAG, "AnyMotion threshold: %d", threshold);

    // Configure AnyMotion debounce (number of consecutive samples above threshold)
    mc3416_set_anymotion_debounce(sensor, 5);

    // Enable AnyMotion interrupt in interrupt control register
    mc3416_intr_ctrl_t intr_cfg = {
        .TILT_INT_EN    = false,
        .FLIP_INT_EN    = false,
        .ANYM_INT_EN    = true,
        .SHAKE_INT_EN   = false,
        .TILT_35_INT_EN = false,
        .RESERVED       = false,
        .AUTO_CLR_EN    = false,
        .ACQ_INT_EN     = false,
    };
    mc3416_set_intr_ctrl(sensor, intr_cfg);
    mc3416_get_intr_ctrl(sensor, &reg_val);
    ESP_LOGI(TAG, "Interrupt ctrl register: 0x%02x", reg_val);

    // Enable AnyMotion detection in motion control register
    mc3416_motion_t motion_cfg = {
        .TF_ENABLE    = false,
        .MOTION_LATCH = true,
        .ANYM_EN      = true,
        .SHAKE_EN     = false,
        .TILT_35_EN   = false,
        .Z_AXIS_ORT   = false,
        .RAW_PROC_STAT = false,
        .MOTION_RESET = false,
    };
    mc3416_set_motion(sensor, motion_cfg);
    mc3416_get_motion(sensor, &reg_val);
    ESP_LOGI(TAG, "Motion ctrl register: 0x%02x", reg_val);
}

static void mc3416_sensor_start(void)
{
    uint8_t reg_val;

    // Enter STANDBY before configuration
    mc3416_set_mode(sensor, MC3416_MODE_STANDBY);

    // Clear any stale interrupts
    mc3416_clear_interrupts(sensor);

    // Configure motion detection
    mc3416_motion_config();

    // Configure range and ODR
    mc3416_set_range(sensor, MC3416_RANGE_2G);
    mc3416_set_sample_rate(sensor, MC3416_SAMPLE_128Hz);

    // Configure INTN pin: active-low, open-drain (default)
    // Optionally enable push-pull with IAH/IPP bits in mode config
    mc3416_mode_config_t mode_cfg = {
        .state       = MC3416_MODE_WAKE,
        .I2C_WDT_NEG = false,
        .I2C_WDT_POS = false,
        .IPP         = false,   // Open-drain (requires external pull-up)
        .IAH         = false,   // Active-low
    };
    mc3416_set_mode_config(sensor, mode_cfg);

    mc3416_get_mode(sensor, &reg_val);
    ESP_LOGI(TAG, "Mode: %s", (reg_val == MC3416_MODE_WAKE) ? "WAKE" : "STANDBY");
}

void app_main(void)
{
    uint8_t chip_id;
    esp_err_t err;

    gpio_intr_init();
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
