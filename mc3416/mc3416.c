/*
 * SPDX-FileCopyrightText: 2024 MEMSIC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_types.h"
#include "mc3416.h"

static const char *TAG = "MC3416";

/**
 * Internal device structure.
 * dev_addr is stored pre-shifted (left by 1) for use in I2C write/read byte.
 */
typedef struct {
    i2c_port_t  bus;
    gpio_num_t  int_pin;
    uint16_t    dev_addr;
    uint32_t    counter;
    float       dt;
    struct timeval *timer;
} mc3416_dev_t;

/* --------------------------------------------------------------------------
 * Low-level I2C helpers
 * -------------------------------------------------------------------------- */

static esp_err_t mc3416_write(mc3416_handle_t sensor, const uint8_t reg_start_addr,
                               const uint8_t *const data_buf, const uint8_t data_len)
{
    mc3416_dev_t *mc3416 = (mc3416_dev_t *)sensor;
    esp_err_t ret = ESP_OK;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ret = i2c_master_start(cmd);
    assert(ESP_OK == ret);
    ret = i2c_master_write_byte(cmd, mc3416->dev_addr, true);
    assert(ESP_OK == ret);
    ret = i2c_master_write_byte(cmd, reg_start_addr, true);
    assert(ESP_OK == ret);
    ret = i2c_master_write(cmd, data_buf, data_len, true);
    assert(ESP_OK == ret);
    ret = i2c_master_stop(cmd);
    assert(ESP_OK == ret);
    ret = i2c_master_cmd_begin(mc3416->bus, cmd, 1000 / portTICK_PERIOD_MS);
    assert(ESP_OK == ret);
    i2c_cmd_link_delete(cmd);

    ESP_LOGD(TAG, "Write reg=0x%02x data=0x%02x", reg_start_addr, *data_buf);
    return ret;
}

static esp_err_t mc3416_read(mc3416_handle_t sensor, const uint8_t reg_start_addr,
                              uint8_t *data_buf, const uint8_t data_len)
{
    mc3416_dev_t *mc3416 = (mc3416_dev_t *)sensor;
    esp_err_t ret = ESP_OK;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ret = i2c_master_start(cmd);
    assert(ESP_OK == ret);
    ret = i2c_master_write_byte(cmd, mc3416->dev_addr, true);
    assert(ESP_OK == ret);
    ret = i2c_master_write_byte(cmd, reg_start_addr, true);
    assert(ESP_OK == ret);
    ret = i2c_master_start(cmd);
    assert(ESP_OK == ret);
    ret = i2c_master_write_byte(cmd, mc3416->dev_addr | 1, true);
    assert(ESP_OK == ret);
    ret = i2c_master_read(cmd, data_buf, data_len, I2C_MASTER_LAST_NACK);
    assert(ESP_OK == ret);
    ret = i2c_master_stop(cmd);
    assert(ESP_OK == ret);
    ret = i2c_master_cmd_begin(mc3416->bus, cmd, 1000 / portTICK_PERIOD_MS);
    assert(ESP_OK == ret);
    i2c_cmd_link_delete(cmd);

    return ret;
}

/* --------------------------------------------------------------------------
 * Handle lifecycle
 * -------------------------------------------------------------------------- */

mc3416_handle_t mc3416_create(i2c_port_t port, const uint16_t dev_addr)
{
    mc3416_dev_t *sensor = (mc3416_dev_t *)calloc(1, sizeof(mc3416_dev_t));
    if (sensor == NULL) {
        ESP_LOGE(TAG, "Failed to allocate sensor structure");
        return NULL;
    }
    sensor->bus      = port;
    sensor->dev_addr = dev_addr << 1;
    sensor->counter  = 0;
    sensor->dt       = 0;
    sensor->timer    = (struct timeval *)malloc(sizeof(struct timeval));
    return (mc3416_handle_t)sensor;
}

void mc3416_delete(mc3416_handle_t sensor)
{
    mc3416_dev_t *mc3416 = (mc3416_dev_t *)sensor;
    if (mc3416) {
        free(mc3416->timer);
        free(mc3416);
    }
}

/* --------------------------------------------------------------------------
 * Identification
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_chip_id(mc3416_handle_t sensor, uint8_t *chip_id)
{
    return mc3416_read(sensor, MC3416_CHIP_ID, chip_id, 1);
}

/* --------------------------------------------------------------------------
 * Operational mode
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_mode(mc3416_handle_t sensor, uint8_t *mode)
{
    uint8_t reg_val;
    esp_err_t ret = mc3416_read(sensor, MC3416_MODE_CTRL, &reg_val, 1);
    if (ret == ESP_OK) {
        *mode = reg_val & 0x03;
    }
    return ret;
}

esp_err_t mc3416_set_mode(mc3416_handle_t sensor, mc3416_mode_t mode)
{
    uint8_t reg_val;
    esp_err_t ret;

    // Read current mode register to preserve IAH, IPP, watchdog bits
    ret = mc3416_read(sensor, MC3416_MODE_CTRL, &reg_val, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    // Bit 2 must always be written as 0 per datasheet
    reg_val = (reg_val & 0xFC) | (mode & 0x03);
    reg_val &= ~(1 << 2);
    return mc3416_write(sensor, MC3416_MODE_CTRL, &reg_val, 1);
}

esp_err_t mc3416_set_mode_config(mc3416_handle_t sensor, mc3416_mode_config_t config)
{
    uint8_t reg_val = 0;
    reg_val |= (config.IAH         ? (1 << 7) : 0);
    reg_val |= (config.IPP         ? (1 << 6) : 0);
    reg_val |= (config.I2C_WDT_POS ? (1 << 5) : 0);
    reg_val |= (config.I2C_WDT_NEG ? (1 << 4) : 0);
    // Bit 3: Reserved, bit 2: must be 0
    reg_val |= (config.state & 0x03);
    return mc3416_write(sensor, MC3416_MODE_CTRL, &reg_val, 1);
}

/* --------------------------------------------------------------------------
 * Acceleration data
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_acceleration(mc3416_handle_t sensor, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t data[6];
    esp_err_t ret = mc3416_read(sensor, MC3416_XOUT_EX_L, data, 6);
    if (ret != ESP_OK) {
        return ret;
    }
    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Range
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_range(mc3416_handle_t sensor, uint8_t *range)
{
    return mc3416_read(sensor, MC3416_RANGE, range, 1);
}

esp_err_t mc3416_set_range(mc3416_handle_t sensor, mc3416_range_t range)
{
    esp_err_t ret;

    ret = mc3416_set_mode(sensor, MC3416_MODE_STANDBY);
    if (ret != ESP_OK) {
        return ret;
    }

    // Bits [6:4] = RANGE[2:0], bit 3 = 1 (required), bits [2:1] = 0 (required), bit 0 = 1 (required)
    uint8_t reg_val = ((range & 0x07) << 4) | 0x09;
    return mc3416_write(sensor, MC3416_RANGE, &reg_val, 1);
}

/* --------------------------------------------------------------------------
 * Sample rate
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_sample_rate(mc3416_handle_t sensor, uint8_t *sr)
{
    return mc3416_read(sensor, MC3416_SAMPLE_RATE, sr, 1);
}

esp_err_t mc3416_set_sample_rate(mc3416_handle_t sensor, mc3416_sample_rate_t sr)
{
    esp_err_t ret;

    ret = mc3416_set_mode(sensor, MC3416_MODE_STANDBY);
    if (ret != ESP_OK) {
        return ret;
    }

    // Only RATE[2:0] (bits [2:0]) are used; upper bits are reserved (write 0)
    uint8_t reg_val = sr & 0x07;
    return mc3416_write(sensor, MC3416_SAMPLE_RATE, &reg_val, 1);
}

/* --------------------------------------------------------------------------
 * Status registers
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_status_reg(mc3416_handle_t sensor, uint8_t *status_reg)
{
    return mc3416_read(sensor, MC3416_STATUS_REG, status_reg, 1);
}

esp_err_t mc3416_get_interrupt_status_reg(mc3416_handle_t sensor, uint8_t *intr_status_reg)
{
    return mc3416_read(sensor, MC3416_INTR_STATUS, intr_status_reg, 1);
}

esp_err_t mc3416_clear_interrupts(mc3416_handle_t sensor)
{
    uint8_t dummy;
    // Reading interrupt status register clears all flags per datasheet
    return mc3416_read(sensor, MC3416_INTR_STATUS, &dummy, 1);
}

/* --------------------------------------------------------------------------
 * Motion control
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_motion(mc3416_handle_t sensor, uint8_t *motion)
{
    return mc3416_read(sensor, MC3416_MOTION_CTRL, motion, 1);
}

esp_err_t mc3416_set_motion(mc3416_handle_t sensor, mc3416_motion_t motion)
{
    uint8_t reg_val = 0;
    reg_val |= (motion.MOTION_RESET  ? (1 << 7) : 0);
    reg_val |= (motion.RAW_PROC_STAT ? (1 << 6) : 0);
    reg_val |= (motion.Z_AXIS_ORT    ? (1 << 5) : 0);
    reg_val |= (motion.TILT_35_EN    ? (1 << 4) : 0);
    reg_val |= (motion.SHAKE_EN      ? (1 << 3) : 0);
    reg_val |= (motion.ANYM_EN       ? (1 << 2) : 0);
    reg_val |= (motion.MOTION_LATCH  ? (1 << 1) : 0);
    reg_val |= (motion.TF_ENABLE     ? (1 << 0) : 0);
    return mc3416_write(sensor, MC3416_MOTION_CTRL, &reg_val, 1);
}

/* --------------------------------------------------------------------------
 * Interrupt control
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_get_intr_ctrl(mc3416_handle_t sensor, uint8_t *intr_ctrl)
{
    return mc3416_read(sensor, MC3416_INTR_CTRL, intr_ctrl, 1);
}

esp_err_t mc3416_set_intr_ctrl(mc3416_handle_t sensor, mc3416_intr_ctrl_t intr_ctrl)
{
    uint8_t reg_val = 0;
    reg_val |= (intr_ctrl.ACQ_INT_EN     ? (1 << 7) : 0);
    reg_val |= (intr_ctrl.AUTO_CLR_EN    ? (1 << 6) : 0);
    // Bit 5: Reserved, write 0
    reg_val |= (intr_ctrl.TILT_35_INT_EN ? (1 << 4) : 0);
    reg_val |= (intr_ctrl.SHAKE_INT_EN   ? (1 << 3) : 0);
    reg_val |= (intr_ctrl.ANYM_INT_EN    ? (1 << 2) : 0);
    reg_val |= (intr_ctrl.FLIP_INT_EN    ? (1 << 1) : 0);
    reg_val |= (intr_ctrl.TILT_INT_EN    ? (1 << 0) : 0);
    return mc3416_write(sensor, MC3416_INTR_CTRL, &reg_val, 1);
}

/* --------------------------------------------------------------------------
 * AnyMotion threshold and debounce
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_write_anymotion_threshold(mc3416_handle_t sensor, uint16_t threshold)
{
    uint8_t data[2];
    threshold &= 0x7FFF;    // Bit 15 must be 0
    data[0] = threshold & 0xFF;
    data[1] = (threshold >> 8) & 0xFF;
    return mc3416_write(sensor, MC3416_AM_THRESH_LSB, data, 2);
}

esp_err_t mc3416_read_anymotion_threshold(mc3416_handle_t sensor, uint16_t *threshold)
{
    uint8_t data[2];
    esp_err_t ret = mc3416_read(sensor, MC3416_AM_THRESH_LSB, data, 2);
    if (ret != ESP_OK) {
        return ret;
    }
    *threshold = ((uint16_t)data[1] << 8) | data[0];
    *threshold &= 0x7FFF;
    return ESP_OK;
}

esp_err_t mc3416_set_anymotion_debounce(mc3416_handle_t sensor, uint8_t debounce)
{
    return mc3416_write(sensor, MC3416_AM_DB, &debounce, 1);
}

esp_err_t mc3416_get_anymotion_debounce(mc3416_handle_t sensor, uint8_t *debounce)
{
    return mc3416_read(sensor, MC3416_AM_DB, debounce, 1);
}

/* --------------------------------------------------------------------------
 * Tilt/flip threshold and debounce
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_write_tf_threshold(mc3416_handle_t sensor, uint16_t threshold)
{
    uint8_t data[2];
    threshold &= 0x7FFF;    // Bit 15 (MSB register bit 7) is reserved
    data[0] = threshold & 0xFF;
    data[1] = (threshold >> 8) & 0x7F;
    return mc3416_write(sensor, MC3416_TF_THRESH_LSB, data, 2);
}

esp_err_t mc3416_read_tf_threshold(mc3416_handle_t sensor, uint16_t *threshold)
{
    uint8_t data[2];
    esp_err_t ret = mc3416_read(sensor, MC3416_TF_THRESH_LSB, data, 2);
    if (ret != ESP_OK) {
        return ret;
    }
    *threshold = (((uint16_t)data[1] & 0x7F) << 8) | data[0];
    return ESP_OK;
}

esp_err_t mc3416_set_tf_debounce(mc3416_handle_t sensor, uint8_t debounce)
{
    return mc3416_write(sensor, MC3416_TF_DB, &debounce, 1);
}

esp_err_t mc3416_get_tf_debounce(mc3416_handle_t sensor, uint8_t *debounce)
{
    return mc3416_read(sensor, MC3416_TF_DB, debounce, 1);
}

/* --------------------------------------------------------------------------
 * Shake threshold
 * -------------------------------------------------------------------------- */

esp_err_t mc3416_write_shake_threshold(mc3416_handle_t sensor, uint16_t threshold)
{
    uint8_t data[2];
    data[0] = threshold & 0xFF;
    data[1] = (threshold >> 8) & 0xFF;
    return mc3416_write(sensor, MC3416_SHK_THRESH_LSB, data, 2);
}

esp_err_t mc3416_read_shake_threshold(mc3416_handle_t sensor, uint16_t *threshold)
{
    uint8_t data[2];
    esp_err_t ret = mc3416_read(sensor, MC3416_SHK_THRESH_LSB, data, 2);
    if (ret != ESP_OK) {
        return ret;
    }
    *threshold = ((uint16_t)data[1] << 8) | data[0];
    return ESP_OK;
}
