#ifndef __MQ7_H
#define __MQ7_H
#include "./SYSTEM/SYS/sys.h"
#include "./BSP/adc/adc.h"  // 使用我们自己的ADC头文件

// 引脚定义（避开 ETH 引脚 PA1/PA7/PG14）
#define MQ7_DO_PIN      GPIO_PIN_15     // 数字输出引脚 PG15
#define MQ7_DO_PORT     GPIOG           // 数字输出端口
#define MQ7_AO_PIN      GPIO_PIN_5      // 模拟输入引脚 PA5 (ADC通道5)
#define MQ7_AO_PORT     GPIOA           // 模拟输入端口 GPIOA
#define MQ7_ADC_CHANNEL ADC_CHANNEL_5   // ADC通道5，使用我们的ADC定义

// 状态定义
#define CO_SAFE         0   // CO浓度安全
#define CO_DANGER       1   // CO浓度危险
#define CO_THRESHOLD    1000 // CO浓度阈值（调整为1000）

// 函数声明
void MQ7_Init(void);                    
uint8_t MQ7_ReadDO(void);              
uint16_t MQ7_ReadAO(void);             
uint8_t MQ7_GetStatus(void);           

#endif
