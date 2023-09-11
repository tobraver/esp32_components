#include "update.h"

void example(void)
{
    uint32_t buff[120] = {};
    
    update_t hupdate;
    hupdate.partition = NULL;
    hupdate.handle = 0;
    
    update_begin(&hupdate);

    update_appened(&hupdate, buff, sizeof(buff));
    update_appened(&hupdate, buff, sizeof(buff));
    update_appened(&hupdate, buff, sizeof(buff));
    update_appened(&hupdate, buff, sizeof(buff));

    update_end(&hupdate);
} 


