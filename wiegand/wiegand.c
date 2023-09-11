#include "wiegand.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h" // ets_delay_us
#include "freertos/task.h" // ets_delay_us

// 偶校验; HID数据校验
uint8_t wiegand_even_parity(wiegand_t *hwiegand)
{
	uint8_t start_idx = 0, end_idx = 0;
	uint8_t bit_val = 0, parity_val = 0;

	if (hwiegand->proto == WIEGAND_PROTO_26)
	{
		start_idx = 16;
		end_idx = 23;
	}
	else if (hwiegand->proto == WIEGAND_PROTO_34)
	{
		start_idx = 16;
		end_idx = 31;
	}

	for (uint8_t idx = start_idx; idx <= end_idx; idx++)
	{
		if (hwiegand->data & ((uint32_t)1 << idx))
		{
			bit_val++;
		}
	}

	if ((bit_val % 2) != 0)
	{
		parity_val = 1;
	}

	return parity_val;
}

// 奇校验; PID数据校验
uint8_t wiegand_odd_parity(wiegand_t *hwiegand)
{
	uint8_t start_idx = 0, end_idx = 0;
	uint8_t bit_val = 0, parity_val = 0;

	if (hwiegand->proto == WIEGAND_PROTO_26)
	{
		start_idx = 0;
		end_idx = 15;
	}
	else if (hwiegand->proto == WIEGAND_PROTO_34)
	{
		start_idx = 0;
		end_idx = 15;
	}

	for (uint8_t idx = start_idx; idx <= end_idx; idx++)
	{
		if (hwiegand->data & ((uint32_t)1 << idx))
		{
			bit_val++;
		}
	}

	if ((bit_val % 2) == 0)
	{
		parity_val = 1;
	}

	return parity_val;
}

void wiegand_init(wiegand_t *hwiegand)
{
	gpio_config_t conf = {0};
	conf.intr_type = GPIO_INTR_DISABLE;
	conf.mode = GPIO_MODE_OUTPUT;
	conf.pin_bit_mask = ((uint64_t)1 << hwiegand->d0_io_num) | ((uint64_t)1 << hwiegand->d1_io_num);
	conf.pull_down_en = 0;
	conf.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config(&conf);

	if (hwiegand->signal_us == 0)
	{
		hwiegand->signal_us = 100;
	}

	if (hwiegand->interval_us == 0)
	{
		hwiegand->interval_us = 1000;
	}
}

void wiegand_set_data0_level(wiegand_t *hwiegand, uint32_t level)
{
	gpio_set_level(hwiegand->d0_io_num, level);
}

void wiegand_set_data1_level(wiegand_t *hwiegand, uint32_t level)
{
	gpio_set_level(hwiegand->d1_io_num, level);
}

void wiegand_delay_us(uint32_t us)
{
	ets_delay_us(us);
}

void wiegand_delay_ms(uint32_t ms)
{
	if(ms)
	{
		vTaskDelay(ms / portTICK_PERIOD_MS);
	}
}

void wiegand_write_bit(wiegand_t *hwiegand, uint8_t bit)
{
	uint32_t active_level = hwiegand->reverse ? 1 : 0;

	if (bit)
	{
		wiegand_set_data1_level(hwiegand, active_level);
		wiegand_delay_us(hwiegand->signal_us);
		wiegand_set_data1_level(hwiegand, !active_level);
	}
	else
	{

		wiegand_set_data0_level(hwiegand, active_level);
		wiegand_delay_us(hwiegand->signal_us);
		wiegand_set_data0_level(hwiegand, !active_level);
	}

	wiegand_delay_ms(hwiegand->interval_us/1000);
}

void wiegand_send_data(wiegand_t *hwiegand)
{
	uint8_t bit_cnt = 0;

	if (hwiegand->proto == WIEGAND_PROTO_26)
	{
		bit_cnt = 24;
	}
	else if (hwiegand->proto == WIEGAND_PROTO_34)
	{
		bit_cnt = 32;
	}

	uint32_t active_level = hwiegand->reverse ? 1 : 0;

	// start
	wiegand_set_data1_level(hwiegand, !active_level);
	wiegand_set_data0_level(hwiegand, !active_level);

	// event
	uint8_t event_val = wiegand_even_parity(hwiegand);
	wiegand_write_bit(hwiegand, event_val);

	// data
	for (uint8_t i = 0; i < bit_cnt; i++)
	{
		uint8_t bit_val = 0;
		if (hwiegand->data & ((uint32_t)1 << (bit_cnt - i - 1)))
		{
			bit_val = 1;
		}
		wiegand_write_bit(hwiegand, bit_val);
	}

	// odd
	uint8_t odd_val = wiegand_odd_parity(hwiegand);
	wiegand_write_bit(hwiegand, odd_val);

	// end
	wiegand_set_data1_level(hwiegand, !active_level);
	wiegand_set_data0_level(hwiegand, !active_level);
}

void wiegand_send(wiegand_t *hwiegand, wiegand_proto proto, uint32_t data)
{
	hwiegand->proto = proto;
	hwiegand->data = data;
	wiegand_send_data(hwiegand);
	printf("wiegand send 0x%08X\n", data);
}

void wiegand26_send(wiegand_t *hwiegand, uint32_t data)
{
	data = data & 0xffffff;
	wiegand_send(hwiegand, WIEGAND_PROTO_26, data);
}

void wiegand34_send(wiegand_t *hwiegand, uint32_t data)
{
	wiegand_send(hwiegand, WIEGAND_PROTO_34, data);
}
