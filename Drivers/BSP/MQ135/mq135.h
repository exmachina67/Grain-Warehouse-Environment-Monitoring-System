#ifndef __MQ135_H
#define __MQ135_H

#include "./SYSTEM/SYS/sys.h"

// I2C引脚定义
#define MQ135_SCL_GPIO    GPIOB
#define MQ135_SCL_PIN     GPIO_PIN_8     // PB8 - SCL
#define MQ135_SDA_GPIO    GPIOB
#define MQ135_SDA_PIN     GPIO_PIN_9     // PB9 - SDA

// I2C操作宏定义
#define SCL_H()    HAL_GPIO_WritePin(MQ135_SCL_GPIO, MQ135_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()    HAL_GPIO_WritePin(MQ135_SCL_GPIO, MQ135_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()    HAL_GPIO_WritePin(MQ135_SDA_GPIO, MQ135_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()    HAL_GPIO_WritePin(MQ135_SDA_GPIO, MQ135_SDA_PIN, GPIO_PIN_RESET)
#define SDA_READ() HAL_GPIO_ReadPin(MQ135_SDA_GPIO, MQ135_SDA_PIN)

// SGP30 I2C地址和命令
#define SGP30_ADDR          0x58    // SGP30的I2C地址
#define SGP30_INIT_CMD      0x2003  // 初始化空气质量测量
#define SGP30_MEASURE_CMD   0x2008  // 测量命令

// 定义CO2浓度阈值和状态
#define CO2_NORMAL      400     // 正常CO2浓度(ppm)
#define CO2_THRESHOLD   700     // CO2浓度阈值(ppm)，超过则危险

// CO2状态枚举
typedef enum {
    CO2_STATUS_NORMAL = 0,  // 正常
    CO2_STATUS_DANGER = 1   // 危险
} CO2_Status;

// 传感器状态结构体
typedef struct {
    uint16_t co2_value;     // CO2浓度值(ppm)
    CO2_Status status;      // 当前状态
    uint8_t initialized;    // 初始化状态
    uint8_t error;         // 错误标志
} SGP30_State;

// 函数声明
void MQ135_Init(void);                    // 初始化
uint16_t MQ135_ReadCO2(void);            // 读取CO2值
uint8_t MQ135_GetStatus(void);           // 获取状态
const char* MQ135_GetStatusString(void);  // 获取状态字符串
SGP30_State MQ135_GetState(void);        // 获取完整状态

#endif
