#ifndef __FC37_H
#define __FC37_H
#include "./SYSTEM/SYS/sys.h"

// 引脚定义（避开 ETH 引脚 PA1/PA7/PG14）
// 注意：FC37_AO 与 ADC 模块共享 PA4；DO 用 PC3 独立（避开 ST021 的 PC2）
#define FC37_DO_PIN      GPIO_PIN_3     // 数字输出引脚 PC3
#define FC37_DO_PORT     GPIOC          // 数字输出端口
#define FC37_AO_PIN      GPIO_PIN_4     // 模拟输入引脚 PA4 (ADC通道4)
#define FC37_AO_PORT     GPIOA          // 模拟输入端口

// 状态定义
#define WATER_NONE       0   // 无水汽 
#define WATER_DETECT     1   // 有水汽
#define RAIN_THRESHOLD   2000 // 雨滴阈值

// 函数声明
void FC37_Init(void);                   
uint8_t FC37_ReadDO(void);              
uint16_t FC37_ReadAO(void);             
uint8_t FC37_GetStatus(void);           

#endif
