#include "ltr303.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "LTR303";
static i2c_bus_handle_t s_i2c_handle;

static float s_ltr303_gain_list[] = {
    [LTR303_GAIN_X1] = 1.0,
    [LTR303_GAIN_X2] = 2.0,
    [LTR303_GAIN_X4] = 4.0,
    [LTR303_GAIN_X8] = 8.0,
    [LTR303_GAIN_X48] = 48.0,
    [LTR303_GAIN_X96] = 96.0,
};

static float s_ltr303_integrate_list[] = {
    [LTR303_INTEGRATE_100MS] = 1.0,
    [LTR303_INTEGRATE_50MS] = 0.5,
    [LTR303_INTEGRATE_200MS] = 2.0,
    [LTR303_INTEGRATE_400MS] = 4.0,
    [LTR303_INTEGRATE_150MS] = 1.5,
    [LTR303_INTEGRATE_250MS] = 2.5,
    [LTR303_INTEGRATE_300MS] = 3.0,
    [LTR303_INTEGRATE_350MS] = 3.5,
};

void ltr303_delay_ms(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

static esp_err_t ltr303_write_regist(uint8_t addr, uint8_t value)
{
    if(s_i2c_handle == NULL) {
        ESP_LOGE(TAG, "(%s) i2c handle is NULL", __func__);
        return ESP_FAIL;
    }
    uint8_t reg[] = { addr, };
    uint8_t data[] = { value, };
    esp_err_t ret = i2c_bus_write_bytes(s_i2c_handle, LTR303_I2C_ADDRESS, reg, sizeof(reg), data, sizeof(data));
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "(%s) i2c bus write bytes failed", __func__);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ltr303_read_regist(uint8_t addr, uint8_t* value, int len)
{
    if(s_i2c_handle == NULL) {
        ESP_LOGE(TAG, "(%s) i2c handle is NULL", __func__);
        return ESP_FAIL;
    }
    uint8_t reg[] = { addr, };
    esp_err_t ret = i2c_bus_read_bytes(s_i2c_handle, LTR303_I2C_ADDRESS, reg, sizeof(reg), value, len);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "(%s) i2c bus read bytes failed", __func__);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// ltr303 part id is 0xA0
esp_err_t ltr303_get_part_id(uint8_t* id)
{
    if(ltr303_read_regist(LTR303_REG_PART_ID, id, 1) != ESP_OK) {
        ESP_LOGE(TAG, "read part id failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ltr303 part id: 0x%02x", *id);
    return ESP_OK;
}

// ltr303 manu id is 0x05
esp_err_t ltr303_get_manu_id(uint8_t* id)
{
    if(ltr303_read_regist(LTR303_REG_MANU_ID, id, 1) != ESP_OK) {
        ESP_LOGE(TAG, "read manu id failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ltr303 manu id: 0x%02x", *id);
    return ESP_OK;
}

esp_err_t ltr303_set_control(ltr303_gain_t gain, ltr303_reset_t reset, ltr303_mode_t mode)
{
    uint8_t ctrl = 0;
    ctrl |= (gain << 2);
    ctrl |= (reset << 1);
    ctrl |= (mode << 0);
    if(ltr303_write_regist(LTR303_REG_ALS_CTRL, ctrl) != ESP_OK) {
        ESP_LOGE(TAG, "write als ctrl failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ltr303 set control success");
    return ESP_OK;
}

esp_err_t ltr303_get_control(ltr303_gain_t* gain, ltr303_reset_t* reset, ltr303_mode_t* mode)
{
    uint8_t ctrl = 0;
    if(ltr303_read_regist(LTR303_REG_ALS_CTRL, &ctrl, sizeof(ctrl)) != ESP_OK) {
        ESP_LOGE(TAG, "read als ctrl failed");
        return ESP_FAIL;
    }
    *gain = (ctrl >> 2) & 0x07;
    *reset = (ctrl >> 1) & 0x01;
    *mode = (ctrl >> 0) & 0x01;
    ESP_LOGI(TAG, "ltr303 get control success, gain: %d, reset: %d, mode: %d", *gain, *reset, *mode);
    return ESP_OK;
}

esp_err_t ltr303_set_measurement_rate(ltr303_integrate_t integrate, ltr303_rate_t rate)
{
    uint8_t meas = 0;
    meas |= (integrate << 3);
    meas |= (rate << 0);
    if(ltr303_write_regist(LTR303_REG_MEAS_RATE, meas) != ESP_OK) {
        ESP_LOGE(TAG, "write als meas rate failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ltr303 set measurement rate success");
    return ESP_OK;
}

esp_err_t ltr303_get_measurement_rate(ltr303_integrate_t* integrate, ltr303_rate_t* rate)
{
    uint8_t meas = 0;
    if(ltr303_read_regist(LTR303_REG_MEAS_RATE, &meas, sizeof(meas)) != ESP_OK) {
        ESP_LOGE(TAG, "read als meas rate failed");
        return ESP_FAIL;
    }
    *integrate = (meas >> 3) & 0x07;
    *rate = (meas >> 0) & 0x07;
    ESP_LOGI(TAG, "ltr303 get measurement rate success, integrate: %d, rate: %d", *integrate, *rate);
    return ESP_OK;
}

esp_err_t ltr303_get_channel_data(uint16_t* channel_0, uint16_t* channel_1)
{
    uint8_t data[4] = { 0 };
    if(ltr303_read_regist(LTR303_REG_CH1DATA, data, sizeof(data)) != ESP_OK) {
        ESP_LOGE(TAG, "read channel data failed");
        return ESP_FAIL;
    }
    *channel_0 = (data[1] << 8) | data[0];
    *channel_1 = (data[3] << 8) | data[2];
    ESP_LOGI(TAG, "ltr303 get channel data success: %d, %d", *channel_0, *channel_1);
    return ESP_OK;
}

// CH0 and CH1 are the sensor values (measurement counts) for Visible + IR (Ch0) and IR only (Ch1) sensors respectively.
// ALS_GAIN is the gain multiplier
// ALS_INT is the integration time in ms/100
float ltr303_get_lux_value(uint16_t channel_0, uint16_t channel_1)
{
    float lux = 0;
    float gain = s_ltr303_gain_list[LTR303_DEV_GAIN];
    float integrate = s_ltr303_integrate_list[LTR303_DEV_INTEGRATE];
    float ratio = channel_1 / (float)(channel_0 + channel_1);
    if (ratio < 0.45) {
        lux = (1.7743 * channel_0 + 1.1059 * channel_1) / gain / integrate;
    } else if (ratio < 0.64) {
        lux = (4.2785 * channel_0 - 1.9548 * channel_1) / gain / integrate;
    } else if (ratio < 0.85) {
        lux = (0.5926 * channel_0 + 0.1185 * channel_1) / gain / integrate;
    }
    return lux;
}

esp_err_t ltr303_init(void)
{
    if(s_i2c_handle != NULL) {
        ESP_LOGI(TAG, "ltr303 already init");
        return ESP_OK;
    }
    ltr303_delay_ms(100); // Wait at least 100 ms - initial startup time
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = LTR303_I2C_SPEED,
        .sda_io_num = LTR303_I2C_SDA,
        .scl_io_num = LTR303_I2C_SCL,
    };
    i2c_bus_handle_t i2c_handle = i2c_bus_create(LTR303_I2C_NUM, &cfg);
    if(i2c_handle == NULL) {
        ESP_LOGE(TAG, "(%s) i2c_bus_create failed", __func__);
        return ESP_FAIL;
    }
    if(i2c_bus_probe_addr(i2c_handle, LTR303_I2C_ADDRESS) != ESP_OK) {
        ESP_LOGE(TAG, "(%s) i2c bus probe addr (0x%02X) failed", __func__, LTR303_I2C_ADDRESS);
        return ESP_FAIL;
    }
    s_i2c_handle = i2c_handle;
    if(ltr303_set_measurement_rate(LTR303_DEV_INTEGRATE, LTR303_DEV_RATE) != ESP_OK) {
        ESP_LOGE(TAG, "ltr303 set measurement rate failed");
        return ESP_FAIL;
    }
    if(ltr303_set_control(LTR303_DEV_GAIN, LTR303_RESET_DISABLE, LTR303_MODE_ACTIVE) != ESP_OK) {
        ESP_LOGE(TAG, "ltr303 set control failed");
        return ESP_FAIL;
    }
    ltr303_delay_ms(10); // Wait at least 10 ms - wakeup time from standby
    ESP_LOGI(TAG, "ltr303 init success");
    return ESP_OK;
}

esp_err_t ltr303_deinit(void)
{
    if(s_i2c_handle != NULL) {
        i2c_bus_delete(s_i2c_handle);
        s_i2c_handle = NULL;
    }
    ESP_LOGW(TAG, "ltr303 deinit success.");
    return ESP_OK;
}

float ltr303_get_light(void)
{
    float light = 0;
    uint16_t channel_0 = 0, channel_1 = 0;
    if(ltr303_get_channel_data(&channel_0, &channel_1) == ESP_OK) {
        light = ltr303_get_lux_value(channel_0, channel_1);
    }
    return light;
}

