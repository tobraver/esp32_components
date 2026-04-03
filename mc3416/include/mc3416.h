/*
 * SPDX-FileCopyrightText: 2024 MEMSIC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MC3416 3-axis accelerometer driver
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/i2c.h"
#include "driver/gpio.h"

// I2C address: determined by VPP pin level at power-up
#define MC3416_I2C_ADDR_0           0x4C    // VPP pin connected to GND
#define MC3416_I2C_ADDR_1           0x6C    // VPP pin connected to VDD
#define MC3416_I2C_MASTER_NUM       I2C_NUM_0

// MC3416 register addresses
#define MC3416_DEV_STATUS_REG       0x05    // Device status register (R)
#define MC3416_INTR_CTRL            0x06    // Interrupt enable register (W)
#define MC3416_MODE_CTRL            0x07    // Mode register (W)
#define MC3416_SAMPLE_RATE          0x08    // Sample rate register (W)
#define MC3416_MOTION_CTRL          0x09    // Motion control register (W)
// 0x0A - 0x0C: RESERVED
#define MC3416_XOUT_EX_L            0x0D    // X-axis accelerometer data LSB (R)
#define MC3416_XOUT_EX_H            0x0E    // X-axis accelerometer data MSB (R)
#define MC3416_YOUT_EX_L            0x0F    // Y-axis accelerometer data LSB (R)
#define MC3416_YOUT_EX_H            0x10    // Y-axis accelerometer data MSB (R)
#define MC3416_ZOUT_EX_L            0x11    // Z-axis accelerometer data LSB (R)
#define MC3416_ZOUT_EX_H            0x12    // Z-axis accelerometer data MSB (R)
#define MC3416_STATUS_REG           0x13    // Status register (R)
#define MC3416_INTR_STATUS          0x14    // Interrupt status register (R)
// 0x15 - 0x17: RESERVED
#define MC3416_CHIP_ID              0x18    // Chip identification register (R)
// 0x19 - 0x1F: RESERVED
#define MC3416_RANGE                0x20    // Range and scale control register (W)
#define MC3416_XOFFL                0x21    // X-axis offset LSB (W)
#define MC3416_XOFFH                0x22    // X-axis offset MSB / XGAIN[8] (W)
#define MC3416_YOFFL                0x23    // Y-axis offset LSB (W)
#define MC3416_YOFFH                0x24    // Y-axis offset MSB / YGAIN[8] (W)
#define MC3416_ZOFFL                0x25    // Z-axis offset LSB (W)
#define MC3416_ZOFFH                0x26    // Z-axis offset MSB / ZGAIN[8] (W)
#define MC3416_XGAIN                0x27    // X-axis gain (W)
#define MC3416_YGAIN                0x28    // Y-axis gain (W)
#define MC3416_ZGAIN                0x29    // Z-axis gain (W)
// 0x2A - 0x3F: RESERVED
#define MC3416_TF_THRESH_LSB        0x40    // Tilt/flip threshold LSB (W)
#define MC3416_TF_THRESH_MSB        0x41    // Tilt/flip threshold MSB (W)
#define MC3416_TF_DB                0x42    // Tilt/flip debounce (W)
#define MC3416_AM_THRESH_LSB        0x43    // AnyMotion threshold LSB (W)
#define MC3416_AM_THRESH_MSB        0x44    // AnyMotion threshold MSB (W)
#define MC3416_AM_DB                0x45    // AnyMotion debounce (W)
#define MC3416_SHK_THRESH_LSB       0x46    // Shake threshold LSB (W)
#define MC3416_SHK_THRESH_MSB       0x47    // Shake threshold MSB (W)
#define MC3416_PK_P2P_DUR_TH_L      0x48    // Peak-to-peak duration threshold LSB (W)
#define MC3416_PK_P2P_DUR_TH_H      0x49    // Shake duration and P2P duration MSB (W)
#define MC3416_TIMER_CTRL           0x4A    // Timer control register (W)

#define MC3416_DEFAULT_CHIP_ID      0xA0    // Chip ID value

/**
 * @brief MC3416 operational state
 *
 * MC3416 only supports STANDBY and WAKE states.
 */
typedef enum {
    MC3416_MODE_STANDBY = 0b00,     // Lowest power, no sampling (default after power-up)
    MC3416_MODE_WAKE    = 0b01,     // Continuous sampling, highest power
} mc3416_mode_t;

/**
 * @brief MC3416 acceleration range
 */
typedef enum {
    MC3416_RANGE_2G  = 0b000,       // ±2g,  sensitivity: 16384 LSB/g
    MC3416_RANGE_4G  = 0b001,       // ±4g,  sensitivity: 8192 LSB/g
    MC3416_RANGE_8G  = 0b010,       // ±8g,  sensitivity: 4096 LSB/g
    MC3416_RANGE_16G = 0b011,       // ±16g, sensitivity: 2048 LSB/g
    MC3416_RANGE_12G = 0b100,       // ±12g, sensitivity: 2730 LSB/g
} mc3416_range_t;

/**
 * @brief MC3416 output data rate (ODR)
 *
 * Supported ODR values as per RATE[2:0] in sample rate register.
 */
typedef enum {
    MC3416_SAMPLE_128Hz  = 0x00,    // 128 Hz  (default)
    MC3416_SAMPLE_256Hz  = 0x01,    // 256 Hz
    MC3416_SAMPLE_512Hz  = 0x02,    // 512 Hz
    MC3416_SAMPLE_1024Hz = 0x05,    // 1024 Hz (fastest)
} mc3416_sample_rate_t;

/**
 * @brief MC3416 motion control register fields
 */
typedef struct {
    bool TF_ENABLE;         // Bit 0: Enable tilt/flip detection
    bool MOTION_LATCH;      // Bit 1: Latch motion block outputs
    bool ANYM_EN;           // Bit 2: Enable AnyMotion detection
    bool SHAKE_EN;          // Bit 3: Enable shake detection (requires ANYM_EN)
    bool TILT_35_EN;        // Bit 4: Enable tilt-35 detection (requires ANYM_EN)
    bool Z_AXIS_ORT;        // Bit 5: Z-axis orientation (0=top, 1=bottom)
    bool RAW_PROC_STAT;     // Bit 6: Raw motion data (0=filtered, 1=raw)
    bool MOTION_RESET;      // Bit 7: Reset motion block (not auto-cleared)
} mc3416_motion_t;

/**
 * @brief MC3416 interrupt enable register fields (register 0x06)
 */
typedef struct {
    bool TILT_INT_EN;       // Bit 0: Enable tilt interrupt
    bool FLIP_INT_EN;       // Bit 1: Enable flip interrupt
    bool ANYM_INT_EN;       // Bit 2: Enable AnyMotion interrupt
    bool SHAKE_INT_EN;      // Bit 3: Enable shake interrupt
    bool TILT_35_INT_EN;    // Bit 4: Enable tilt-35 interrupt
    bool RESERVED;          // Bit 5: Reserved (write 0)
    bool AUTO_CLR_EN;       // Bit 6: Auto-clear interrupts when condition clears
    bool ACQ_INT_EN;        // Bit 7: Enable interrupt after each sample acquisition
} mc3416_intr_ctrl_t;

/**
 * @brief MC3416 interrupt status register fields (register 0x14, read-only)
 *
 * All bits are cleared when this register is read.
 */
typedef struct {
    bool TILT_INT;          // Bit 0: Tilt interrupt pending
    bool FLIP_INT;          // Bit 1: Flip interrupt pending
    bool ANYM_INT;          // Bit 2: AnyMotion interrupt pending
    bool SHAKE_INT;         // Bit 3: Shake interrupt pending
    bool TILT_35_INT;       // Bit 4: Tilt-35 interrupt pending
    bool RESERVED_5;        // Bit 5: Reserved
    bool RESERVED_6;        // Bit 6: Reserved
    bool ACQ_INT;           // Bit 7: Sample acquisition interrupt pending
} mc3416_intr_status_t;

/**
 * @brief MC3416 mode register configuration
 *
 * Controls interrupt pin polarity/drive mode, watchdog timer, and operational state.
 */
typedef struct {
    mc3416_mode_t state;    // Bits [1:0]: Operational state (STANDBY/WAKE)
    bool I2C_WDT_NEG;       // Bit 4: Watchdog timer for negative SCL stalls
    bool I2C_WDT_POS;       // Bit 5: Watchdog timer for positive SCL stalls
    bool IPP;               // Bit 6: INTN pin drive mode (0=open-drain, 1=push-pull)
    bool IAH;               // Bit 7: INTN pin polarity (0=active-low, 1=active-high)
} mc3416_mode_config_t;

/**
 * @brief MC3416 driver configuration
 */
typedef struct {
    uint8_t mode;
    uint8_t range;
    uint8_t sample_rate;
} mc3416_config_t;

typedef void *mc3416_handle_t;

/**
 * @brief Create a new MC3416 sensor handle
 *
 * @param port      I2C port number
 * @param dev_addr  7-bit I2C device address (MC3416_I2C_ADDR_0 or MC3416_I2C_ADDR_1)
 * @return          Sensor handle, or NULL on failure
 */
mc3416_handle_t mc3416_create(i2c_port_t port, const uint16_t dev_addr);

/**
 * @brief Delete a MC3416 sensor handle and free resources
 *
 * @param sensor    Sensor handle
 */
void mc3416_delete(mc3416_handle_t sensor);

/**
 * @brief Read the chip identification register
 *
 * @param sensor    Sensor handle
 * @param chip_id   Pointer to store chip ID (expected: MC3416_DEFAULT_CHIP_ID = 0xA0)
 * @return          ESP_OK on success
 */
esp_err_t mc3416_get_chip_id(mc3416_handle_t sensor, uint8_t *chip_id);

/**
 * @brief Read the current operational state from mode register
 *
 * @param sensor    Sensor handle
 * @param mode      Pointer to store current mode (STATE[1:0] bits)
 * @return          ESP_OK on success
 */
esp_err_t mc3416_get_mode(mc3416_handle_t sensor, uint8_t *mode);

/**
 * @brief Set the operational state (STANDBY or WAKE)
 *
 * Performs a read-modify-write to preserve IAH, IPP, and watchdog bits.
 *
 * @param sensor    Sensor handle
 * @param mode      Target mode (mc3416_mode_t)
 * @return          ESP_OK on success
 */
esp_err_t mc3416_set_mode(mc3416_handle_t sensor, mc3416_mode_t mode);

/**
 * @brief Configure mode register (state + interrupt pin + watchdog)
 *
 * @param sensor    Sensor handle
 * @param config    Mode configuration structure
 * @return          ESP_OK on success
 */
esp_err_t mc3416_set_mode_config(mc3416_handle_t sensor, mc3416_mode_config_t config);

/**
 * @brief Read all three axis acceleration values
 *
 * Returns raw 16-bit signed 2's complement values. Device must be in WAKE state.
 *
 * @param sensor    Sensor handle
 * @param x         Pointer to X-axis output
 * @param y         Pointer to Y-axis output
 * @param z         Pointer to Z-axis output
 * @return          ESP_OK on success
 */
esp_err_t mc3416_get_acceleration(mc3416_handle_t sensor, int16_t *x, int16_t *y, int16_t *z);

/**
 * @brief Read the current acceleration range setting
 *
 * @param sensor    Sensor handle
 * @param range     Pointer to store RANGE register value
 * @return          ESP_OK on success
 */
esp_err_t mc3416_get_range(mc3416_handle_t sensor, uint8_t *range);

/**
 * @brief Set the acceleration range
 *
 * Device is set to STANDBY before changing range, per datasheet requirement.
 * Caller must re-enable WAKE mode after configuration.
 *
 * @param sensor    Sensor handle
 * @param range     Target range (mc3416_range_t)
 * @return          ESP_OK on success
 */
esp_err_t mc3416_set_range(mc3416_handle_t sensor, mc3416_range_t range);

/**
 * @brief Read the current sample rate setting
 *
 * @param sensor    Sensor handle
 * @param sr        Pointer to store sample rate register value
 * @return          ESP_OK on success
 */
esp_err_t mc3416_get_sample_rate(mc3416_handle_t sensor, uint8_t *sr);

/**
 * @brief Set the output data rate
 *
 * Device is set to STANDBY before changing, per datasheet requirement.
 * Caller must re-enable WAKE mode after configuration.
 *
 * @param sensor    Sensor handle
 * @param sr        Target sample rate (mc3416_sample_rate_t)
 * @return          ESP_OK on success
 */
esp_err_t mc3416_set_sample_rate(mc3416_handle_t sensor, mc3416_sample_rate_t sr);

/**
 * @brief Read the status register (0x13)
 *
 * Contains NEW_DATA, TILT_FLAG, FLIP_FLAG, ANYM_FLAG, SHAKE_FLAG, TILT_35_FLAG.
 * Reading clears the NEW_DATA bit.
 *
 * @param sensor        Sensor handle
 * @param status_reg    Pointer to store status register value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_get_status_reg(mc3416_handle_t sensor, uint8_t *status_reg);

/**
 * @brief Read the interrupt status register (0x14)
 *
 * Reading this register clears all pending interrupt flags and the I2C_WDT bit.
 *
 * @param sensor            Sensor handle
 * @param intr_status_reg   Pointer to store interrupt status register value
 * @return                  ESP_OK on success
 */
esp_err_t mc3416_get_interrupt_status_reg(mc3416_handle_t sensor, uint8_t *intr_status_reg);

/**
 * @brief Clear all pending interrupts by reading interrupt status register
 *
 * @param sensor    Sensor handle
 * @return          ESP_OK on success
 */
esp_err_t mc3416_clear_interrupts(mc3416_handle_t sensor);

/**
 * @brief Read the motion control register
 *
 * @param sensor    Sensor handle
 * @param motion    Pointer to store motion control register value
 * @return          ESP_OK on success
 */
esp_err_t mc3416_get_motion(mc3416_handle_t sensor, uint8_t *motion);

/**
 * @brief Configure motion detection features
 *
 * Device should be in STANDBY state before calling this function.
 *
 * @param sensor    Sensor handle
 * @param motion    Motion configuration structure
 * @return          ESP_OK on success
 */
esp_err_t mc3416_set_motion(mc3416_handle_t sensor, mc3416_motion_t motion);

/**
 * @brief Read the interrupt enable register
 *
 * @param sensor        Sensor handle
 * @param intr_ctrl     Pointer to store interrupt enable register value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_get_intr_ctrl(mc3416_handle_t sensor, uint8_t *intr_ctrl);

/**
 * @brief Configure interrupt enable register
 *
 * Device should be in STANDBY state before calling this function.
 *
 * @param sensor        Sensor handle
 * @param intr_ctrl     Interrupt enable configuration structure
 * @return              ESP_OK on success
 */
esp_err_t mc3416_set_intr_ctrl(mc3416_handle_t sensor, mc3416_intr_ctrl_t intr_ctrl);

/**
 * @brief Write AnyMotion threshold value (15-bit, MSB ignored)
 *
 * @param sensor        Sensor handle
 * @param threshold     15-bit threshold value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_write_anymotion_threshold(mc3416_handle_t sensor, uint16_t threshold);

/**
 * @brief Read AnyMotion threshold value
 *
 * @param sensor        Sensor handle
 * @param threshold     Pointer to store 15-bit threshold value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_read_anymotion_threshold(mc3416_handle_t sensor, uint16_t *threshold);

/**
 * @brief Set AnyMotion debounce count
 *
 * @param sensor        Sensor handle
 * @param debounce      8-bit debounce value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_set_anymotion_debounce(mc3416_handle_t sensor, uint8_t debounce);

/**
 * @brief Read AnyMotion debounce count
 *
 * @param sensor        Sensor handle
 * @param debounce      Pointer to store debounce value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_get_anymotion_debounce(mc3416_handle_t sensor, uint8_t *debounce);

/**
 * @brief Write tilt/flip threshold value (15-bit)
 *
 * @param sensor        Sensor handle
 * @param threshold     15-bit threshold value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_write_tf_threshold(mc3416_handle_t sensor, uint16_t threshold);

/**
 * @brief Read tilt/flip threshold value
 *
 * @param sensor        Sensor handle
 * @param threshold     Pointer to store 15-bit threshold value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_read_tf_threshold(mc3416_handle_t sensor, uint16_t *threshold);

/**
 * @brief Set tilt/flip debounce duration
 *
 * @param sensor        Sensor handle
 * @param debounce      8-bit debounce value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_set_tf_debounce(mc3416_handle_t sensor, uint8_t debounce);

/**
 * @brief Read tilt/flip debounce duration
 *
 * @param sensor        Sensor handle
 * @param debounce      Pointer to store debounce value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_get_tf_debounce(mc3416_handle_t sensor, uint8_t *debounce);

/**
 * @brief Write shake threshold value (16-bit)
 *
 * @param sensor        Sensor handle
 * @param threshold     16-bit threshold value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_write_shake_threshold(mc3416_handle_t sensor, uint16_t threshold);

/**
 * @brief Read shake threshold value
 *
 * @param sensor        Sensor handle
 * @param threshold     Pointer to store 16-bit threshold value
 * @return              ESP_OK on success
 */
esp_err_t mc3416_read_shake_threshold(mc3416_handle_t sensor, uint16_t *threshold);

#ifdef __cplusplus
}
#endif
