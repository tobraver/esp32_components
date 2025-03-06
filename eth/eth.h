#ifndef __ETH_H__
#define __ETH_H__

#if __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void eth_init(void);
void eth_wait_connected(void);
bool eth_config(char* ip, char* mask, char* gw, char* dns1, char* dns2);
bool eth_got_ip(void);
char* eth_get_ip(void);
char* eth_get_mask(void);
char* eth_get_gw(void);
char* eth_get_dns1(void);
char* eth_get_dns2(void);


#if __cplusplus
}
#endif

#endif // !__ETH_H__
