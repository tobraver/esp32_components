#ifndef __CH422_H__
#define __CH422_H__

#include "stdio.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

#define CH422_I2C_NUM I2C_NUM_1
#define CH422_I2C_SCL GPIO_NUM_6
#define CH422_I2C_SDA GPIO_NUM_2
#define CH422_I2C_SPEED 100000

typedef enum {
    CH422_IO_MODE_INPUT,
    CH422_IO_MODE_OUTPUT,
} ch422_io_mode_t;

typedef enum {
    CH422_IO_NUM_0, // input io
    CH422_IO_NUM_1,
    CH422_IO_NUM_2,
    CH422_IO_NUM_3,
    CH422_IO_NUM_4,
    CH422_IO_NUM_5,
    CH422_IO_NUM_6,
    CH422_IO_NUM_7,
    CH422_IO_NUM_8, // output io
    CH422_IO_NUM_9,
    CH422_IO_NUM_10,
    CH422_IO_NUM_11,
    CH422_IO_NUM_MAX,
} ch422_io_num_t;

typedef enum {
    CH422_IO_LOW,
    CH422_IO_HIGH,
} ch422_io_level_t;

esp_err_t ch422_init(void);
esp_err_t ch422_deinit(void);
int ch422_get_level(ch422_io_num_t io_num);
esp_err_t ch422_set_level(ch422_io_num_t io_num, uint8_t level);

#endif // __CH422_H__
