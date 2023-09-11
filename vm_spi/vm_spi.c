#include "vm_spi.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h" // ets_delay_us

void vm_spi_sck(vm_spi_t* hspi, uint32_t level)
{
    gpio_set_level(hspi->sck_io_num, level);
}

void vm_spi_mosi(vm_spi_t* hspi, uint32_t level)
{
    gpio_set_level(hspi->mosi_io_num, level);
}

void vm_spi_cs(vm_spi_t* hspi, uint32_t level)
{
    gpio_set_level(hspi->cs_io_num, level);
}

uint8_t vm_spi_miso(vm_spi_t* hspi)
{
    return gpio_get_level(hspi->miso_io_num) ? 1: 0;
}

void vm_spi_delay_us(uint32_t us)
{
	// ets_delay_us(us);
}


void vm_spi_init(vm_spi_t* hspi)
{
	gpio_config_t conf;
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.mode = GPIO_MODE_INPUT;
    conf.pin_bit_mask = ((uint64_t)1 << hspi->miso_io_num);
    conf.pull_down_en = 0;
    conf.pull_up_en = 0;
    gpio_config(&conf);

    conf.mode = GPIO_MODE_OUTPUT;
    conf.pin_bit_mask = ((uint64_t)1 << hspi->sck_io_num) | ((uint64_t)1 << hspi->mosi_io_num) | ((uint64_t)1 << hspi->cs_io_num);
    gpio_config(&conf);
}

void vm_spi_select(vm_spi_t* hspi, uint32_t select)
{
	uint32_t level = 1;
	if(select)
	{
		level = 0;
	}
	vm_spi_cs(hspi, level);
}

// Mode0 MSB
uint8_t vm_spi_trans_byte(vm_spi_t* hspi, uint8_t byte)
{
	uint8_t rx_data = 0;

    vm_spi_sck(hspi, 0);
	for (uint8_t i = 0; i < 8; i++)
	{
		if (byte & (1 << 7))
            vm_spi_mosi(hspi, 1);
		else
			vm_spi_mosi(hspi, 0);

		vm_spi_delay_us(1);
        vm_spi_sck(hspi, 1);

		rx_data <<= 1;
		rx_data |= vm_spi_miso(hspi);
		vm_spi_delay_us(1);
        vm_spi_sck(hspi, 0);
		
		byte <<= 1;
	}
    vm_spi_sck(hspi, 0);

	return rx_data;
}

uint16_t vm_spi_write_buffer(vm_spi_t* hspi, uint8_t *buf, uint16_t size)
{
	uint16_t r = size;
	while (size--)
	{
		vm_spi_trans_byte(hspi, *buf);
		buf++;
	}
	return r;
}

uint16_t vm_spi_read_buffer(vm_spi_t* hspi, uint8_t *buf, uint16_t size)
{
	uint16_t r = size;
	while (size--)
	{
		*buf = vm_spi_trans_byte(hspi, 0x00);
		buf++;
	}
	return r;
}

uint16_t vm_spi_trans_buffer(vm_spi_t* hspi, uint8_t *buf_wr, uint16_t size, uint8_t *buf_rd)
{
	uint16_t r = size;
	while (size--)
	{
		*buf_rd = vm_spi_trans_byte(hspi, *buf_wr);
		buf_wr++;
		buf_rd++;
	}
	return r;
}