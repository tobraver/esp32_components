#ifndef __LTR303_H__
#define __LTR303_H__

#include "stdio.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

#define LTR303_I2C_NUM          I2C_NUM_0
#define LTR303_I2C_SCL          GPIO_NUM_18
#define LTR303_I2C_SDA          GPIO_NUM_17
#define LTR303_I2C_SPEED        100000
#define LTR303_I2C_ADDRESS      0x52 // 0x29<<1

// Config
#define LTR303_DEV_GAIN         LTR303_GAIN_X1
#define LTR303_DEV_INTEGRATE    LTR303_INTEGRATE_100MS
#define LTR303_DEV_RATE         LTR303_RATE_500MS

// Register
#define LTR303_REG_ALS_CTRL     0x80
#define LTR303_REG_MEAS_RATE    0x85
#define LTR303_REG_PART_ID      0x86
#define LTR303_REG_MANU_ID      0x87
#define LTR303_REG_CH1DATA      0x88

typedef enum {
    LTR303_GAIN_X1  = 0, // 1lux to 64k lux (default)
    LTR303_GAIN_X2  = 1, // 0.5lux to 32k lux
    LTR303_GAIN_X4  = 2, // 0.25lux to 16k lux
    LTR303_GAIN_X8  = 3, // 0.125lux to 8k lux
    LTR303_GAIN_X48 = 6, // 0.02lux to 1.3k lux
    LTR303_GAIN_X96 = 7, // 0.01lux to 600 lux
} ltr303_gain_t;

typedef enum {
    LTR303_RESET_DISABLE = 0,
    LTR303_RESET_ENABLE  = 1,
} ltr303_reset_t;

typedef enum {
    LTR303_MODE_STANDBY = 0,
    LTR303_MODE_ACTIVE  = 1,
} ltr303_mode_t;

typedef enum {
    LTR303_INTEGRATE_100MS = 0, // default
    LTR303_INTEGRATE_50MS  = 1,
    LTR303_INTEGRATE_200MS = 2,
    LTR303_INTEGRATE_400MS = 3,
    LTR303_INTEGRATE_150MS = 4,
    LTR303_INTEGRATE_250MS = 5,
    LTR303_INTEGRATE_300MS = 6,
    LTR303_INTEGRATE_350MS = 7,
} ltr303_integrate_t;

typedef enum {
    LTR303_RATE_50MS   = 0,
    LTR303_RATE_100MS  = 1,
    LTR303_RATE_200MS  = 2,
    LTR303_RATE_500MS  = 3, // default
    LTR303_RATE_1000MS = 4,
    LTR303_RATE_2000MS = 5,
} ltr303_rate_t;

esp_err_t ltr303_init(void);
esp_err_t ltr303_deinit(void);
float ltr303_get_light(void);

#endif // __LTR303_H__
