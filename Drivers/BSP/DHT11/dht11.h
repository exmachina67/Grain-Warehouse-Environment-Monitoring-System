#ifndef __DS18B20_H
#define __DS18B20_H
#include "./SYSTEM/SYS/sys.h"
//////////////////////////////////////////////////////////////////////////////////
// ????:DS18B20???????????
// ??:ALIENTEK STM32F407
// DHT11?????
// ????:2017/4/14
// ??:V1.0
// ????:2014-2024
// All rights reserved
//////////////////////////////////////////////////////////////////////////////////

//// IO???
//#define DHT11_IO_IN()  {GPIOG->MODER&=~(3<<(8*2));GPIOG->MODER|=0<<(8*2);}  // PG8 ???????
//#define DHT11_IO_OUT() {GPIOG->MODER&=~(3<<(8*2));GPIOG->MODER|=1<<(8*2);}  // PG8 ???????

//// IO??????
//#define DHT11_DQ_OUT    PGout(8) // ????? PG8
//#define DHT11_DQ_IN     PGin(8)  // ? PG8 ????

// 设置 PG8 为输入
#define DHT11_IO_IN()   do{ \
                            GPIOG->MODER &= ~(3 << (8 * 2)); \
                            GPIOG->MODER |= 0 << (8 * 2); \
                        }while(0)

// 设置 PG8 为输出
#define DHT11_IO_OUT()  do{ \
                            GPIOG->MODER &= ~(3 << (8 * 2)); \
                            GPIOG->MODER |= 1 << (8 * 2); \
                        }while(0)

// 读取输入数据寄存器
#define DHT11_DQ_IN     ((GPIOG->IDR & GPIO_PIN_8) ? 1 : 0)

// 设置输出数据寄存器
#define DHT11_DQ_OUT(x) do{ \
                            if(x) GPIOG->ODR |= GPIO_PIN_8; \
                            else GPIOG->ODR &= ~GPIO_PIN_8; \
                        }while(0)

uint8_t DHT11_Init(void);           // ??? DHT11
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi);  // ???????
uint8_t DHT11_Read_Byte(void);      // ????????
uint8_t DHT11_Read_Bit(void);       // ????????
uint8_t DHT11_Check(void);          // ?? DHT11 ??????
void DHT11_Rst(void);           // ?? DHT11  

#endif
