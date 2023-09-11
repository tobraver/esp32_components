#ifndef __WIEGAND_H__
#define __WIEGAND_H__

#include "stdio.h"
#include "stdint.h"

typedef enum
{
    WIEGAND_PROTO_26,
    WIEGAND_PROTO_34,
} wiegand_proto;

typedef struct 
{
    wiegand_proto proto;
    int32_t       d0_io_num; // D0
    int32_t       d1_io_num; // D1
    uint32_t      data;
    uint32_t      reverse;
    uint32_t      signal_us; // default 100
    uint32_t      interval_us; // default 2000, set x*1000
} wiegand_t;

void wiegand_init(wiegand_t *hwiegand);
void wiegand26_send(wiegand_t *hwiegand, uint32_t data);
void wiegand34_send(wiegand_t *hwiegand, uint32_t data);


#endif // !__WIEGAND_H__
