#ifndef __ATK_MW1278_RUN_H
#define __ATK_MW1278_RUN_H

#include "./SYSTEM/SYS/sys.h"

void atk_mw1278_init(void);
void atk_mw1278_send(uint16_t temp, uint16_t humi, uint32_t CO, uint32_t CO2, uint8_t dry);

#endif

