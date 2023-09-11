#ifndef __VM_SPI_H__
#define __VM_SPI_H__

#include "stdio.h"
#include "stdint.h"

typedef struct 
{
    int sck_io_num;
    int mosi_io_num;
    int miso_io_num;
    int cs_io_num;
} vm_spi_t;

void vm_spi_init(vm_spi_t* hspi);
void vm_spi_select(vm_spi_t* hspi, uint32_t select);
uint16_t vm_spi_write_buffer(vm_spi_t* hspi, uint8_t *buf, uint16_t size);
uint16_t vm_spi_read_buffer(vm_spi_t* hspi, uint8_t *buf, uint16_t size);
uint16_t vm_spi_trans_buffer(vm_spi_t* hspi, uint8_t *buf_wr, uint16_t size, uint8_t *buf_rd);

#endif // !__VM_SPI_H__
