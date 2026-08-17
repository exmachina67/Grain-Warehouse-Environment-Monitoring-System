#include "./BSP/mq7/mq7.h"
#include "./BSP/adc/adc.h"
#include "./SYSTEM/delay/delay.h"
#include "stdio.h"

// MQ7 初始化
void MQ7_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();  // DO引脚时钟
    __HAL_RCC_GPIOA_CLK_ENABLE();  // AO引脚时钟

    // 配置数字输出引脚
    GPIO_InitStruct.Pin = MQ7_DO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(MQ7_DO_PORT, &GPIO_InitStruct);

    // 配置模拟输入引脚
    GPIO_InitStruct.Pin = MQ7_AO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MQ7_AO_PORT, &GPIO_InitStruct);
    
    
}

// 获取数字值
uint8_t MQ7_ReadDO(void)
{
    static uint8_t last_status = 0;
    uint8_t current_status = HAL_GPIO_ReadPin(MQ7_DO_PORT, MQ7_DO_PIN);
    
    // 消抖处理
    if(current_status != last_status)
    {
        delay_ms(10);
        current_status = HAL_GPIO_ReadPin(MQ7_DO_PORT, MQ7_DO_PIN);
    }
    last_status = current_status;
    
    return current_status;
}

uint16_t MQ7_ReadAO(void)
{
    uint16_t adc_value = adc_get_result_average(ADC_ADCX_CHY_1, 20);  // 使用ADC通道1,20次平均
    
    // 添加调试信息
//    printf("MQ7 Raw ADC Value: %d\r\n", adc_value);
    
    return adc_value;
}

uint8_t MQ7_GetStatus(void)
{
    static uint8_t last_status = 0;
    uint16_t ao_value = MQ7_ReadAO();
    uint8_t current_status;
    
    // CO浓度判断,AO值过高,DO输出低电平
    if(ao_value > CO_THRESHOLD || MQ7_ReadDO() == 0)
    {
        current_status = CO_DANGER;
    }
    else
    {
        current_status = CO_SAFE;
    }
    
    // 状态滤波
    if(current_status != last_status)
    {
        delay_ms(10);
        last_status = current_status;
    }
    
    return current_status;
}
