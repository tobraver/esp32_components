#include "ch422.h"
#include "i2c_bus.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "audio_mutex.h"

static const char *TAG = "CH422";
typedef struct {
    xSemaphoreHandle mutex;
    uint8_t          out_value;
    i2c_bus_handle_t i2c_handle;
} ch422_desc_t;

ch422_desc_t s_ch422_desc = {
    .out_value = 0x0F, // default high level
};

esp_err_t ch422_write_level(uint8_t value);

void ch422_mutex_lock(void)
{
    if(s_ch422_desc.mutex) {
        mutex_lock(s_ch422_desc.mutex);
    }
}

void ch422_mutex_unlock(void)
{
    if(s_ch422_desc.mutex) {
        mutex_unlock(s_ch422_desc.mutex);
    }
}

void ch422_set_out_value(uint8_t value)
{
    ch422_mutex_lock();
    s_ch422_desc.out_value = value;
    ch422_mutex_unlock();
}

uint8_t ch422_get_out_value(void)
{
    uint8_t value = 0;
    ch422_mutex_lock();
    value = s_ch422_desc.out_value;
    ch422_mutex_unlock();
    return value;
}

typedef struct {
    i2c_config_t     i2c_conf;   /*!<I2C bus parameters*/
    i2c_port_t       i2c_port;   /*!<I2C port number */
    int              ref_count;  /*!<Reference Count for multiple client */
    xSemaphoreHandle bus_lock;   /*!<Lock for bus */
} i2c_bus_t;

static esp_err_t ch422_write_operate(uint8_t operate, uint8_t value)
{
    if(s_ch422_desc.i2c_handle == NULL) {
        ESP_LOGE(TAG, "(%s) i2c handle is NULL", __func__);
        return ESP_FAIL;
    }
    i2c_bus_t* p_bus = (i2c_bus_t*)s_ch422_desc.i2c_handle;
    esp_err_t ret = ESP_OK;
    mutex_lock(p_bus->bus_lock);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ret |= i2c_master_start(cmd);
    ret |= i2c_master_write(cmd, &operate, 1, 1);
    ret |= i2c_master_write(cmd, &value, 1, 1);
    ret |= i2c_master_stop(cmd);
    ret |= i2c_master_cmd_begin(p_bus->i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    mutex_unlock(p_bus->bus_lock);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "(%s) ch422 write operate:%d, value:%d failed", __func__, operate, value);
    }
    return ret;
}

static esp_err_t ch422_read_operate(uint8_t operate, uint8_t* value)
{
    if(s_ch422_desc.i2c_handle == NULL) {
        ESP_LOGE(TAG, "(%s) i2c handle is NULL", __func__);
        return ESP_FAIL;
    }
    i2c_bus_t* p_bus = (i2c_bus_t*)s_ch422_desc.i2c_handle;
    esp_err_t ret = ESP_OK;
    mutex_lock(p_bus->bus_lock);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ret |= i2c_master_start(cmd);
    ret |= i2c_master_write(cmd, &operate, 1, 1);
    ret |= i2c_master_read_byte(cmd, value, 1);
    ret |= i2c_master_stop(cmd);
    ret |= i2c_master_cmd_begin(p_bus->i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    mutex_unlock(p_bus->bus_lock);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "(%s) ch422 read operate:%d failed", __func__, operate);
    }
    return ret;
}

esp_err_t ch422_init(void)
{
    if(s_ch422_desc.i2c_handle != NULL) {
        ESP_LOGI(TAG, "ch422 already init");
        return ESP_OK;
    }
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CH422_I2C_SPEED,
        .sda_io_num = CH422_I2C_SDA,
        .scl_io_num = CH422_I2C_SCL,
    };
    i2c_bus_handle_t i2c_handle = i2c_bus_create(CH422_I2C_NUM, &cfg);
    if(i2c_handle == NULL) {
        ESP_LOGE(TAG, "(%s) i2c_bus_create failed", __func__);
        return ESP_FAIL;
    }
    s_ch422_desc.i2c_handle = i2c_handle;
    // IO0~IO7 input, IO8~IO11 output
    if(ch422_write_operate(0x48, 0x00) != ESP_OK) {
        ESP_LOGE(TAG, "(%s) ch422 config io mode failed", __func__);
        return ESP_FAIL;
    }
    // IO8~IO11 default output 1
    if(ch422_write_level(0x0F) != ESP_OK) {
        ESP_LOGE(TAG, "(%s) ch422 output level failed", __func__);
        return ESP_FAIL;
    }
    s_ch422_desc.mutex = mutex_create();
    ESP_LOGI(TAG, "ch422 init success");
    return ESP_OK;
}

esp_err_t ch422_deinit(void)
{
    if(s_ch422_desc.i2c_handle != NULL) {
        i2c_bus_delete(s_ch422_desc.i2c_handle);
        s_ch422_desc.i2c_handle = NULL;
    }
    ESP_LOGW(TAG, "ch422 deinit success.");
    return ESP_OK;
}

static uint8_t ch422_read_level(void)
{
    uint8_t value = 0;
    ch422_read_operate(0X4D, &value);
    return value;
}

int ch422_get_level(ch422_io_num_t io_num)
{
    int level = 0;
    if(io_num > CH422_IO_NUM_8) {
        ESP_LOGW(TAG, "(%s) io %d mode is output", __func__, io_num);
        return level;
    }
    if(ch422_read_level() & (1 << io_num)) {
        level = 1;
    }
    return level;
}

esp_err_t ch422_write_level(uint8_t value)
{
    ch422_set_out_value(value);
    return ch422_write_operate(0X46, value);
}

esp_err_t ch422_set_level(ch422_io_num_t io_num, uint8_t level)
{
    if(io_num < CH422_IO_NUM_8) {
        ESP_LOGW(TAG, "(%s) io %d mode is input", __func__, io_num);
        return ESP_FAIL;
    }
    if(io_num >= CH422_IO_NUM_MAX) {
        ESP_LOGI(TAG, "(%s) io %d is invalid", __func__, io_num);
        return ESP_OK;
    }
    uint8_t value = ch422_get_out_value();
    if(level) {
        value |= (1 << (io_num - CH422_IO_NUM_8));
    } else {
        value &= ~(1 << (io_num - CH422_IO_NUM_8));
    }
    value = value & 0x0F;
    return ch422_write_level(value);
}

