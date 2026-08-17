#include "./BSP/fc37/fc37.h"
#include "./BSP/adc/adc.h"
#include "./SYSTEM/delay/delay.h"
#include "stdio.h"

void FC37_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();  // DO引脚时钟（GPIOC for PC2）
    __HAL_RCC_GPIOA_CLK_ENABLE();  // AO引脚时钟（GPIOA for PA4）

    // 配置PA4为数字输入
    GPIO_InitStruct.Pin = FC37_DO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(FC37_DO_PORT, &GPIO_InitStruct);

    // 配置PA7为模拟输入
    GPIO_InitStruct.Pin = FC37_AO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(FC37_AO_PORT, &GPIO_InitStruct);
    
   
    
}

uint8_t FC37_ReadDO(void)
{
    static uint8_t last_status = 0;
    uint8_t current_status = HAL_GPIO_ReadPin(FC37_DO_PORT, FC37_DO_PIN);
    
    // 添加去抖动处理
    if(current_status != last_status)
    {
        delay_ms(10);  // 延时消抖
        current_status = HAL_GPIO_ReadPin(FC37_DO_PORT, FC37_DO_PIN);
    }
    last_status = current_status;
    
    return current_status;
}

uint16_t FC37_ReadAO(void)
{
    uint16_t adcx;
    
    // 直接使用ADC通道4（PA4）
    adcx = adc_get_result(ADC_CHANNEL_4);  // 先读取一次单次值
    
    // 然后读取平均值
    adcx = adc_get_result_average(ADC_CHANNEL_4, 20);
    
    return adcx;
}

uint8_t FC37_GetStatus(void)
{
    uint16_t ao_value = FC37_ReadAO();  // 读取模拟值
    uint8_t current_status = 1;  // 默认状态为1

    // 判断逻辑：低于1800认为有水，返回0；高于1800认为干燥，返回1
    if(ao_value < 1800)  
    {
        current_status = 0;  // 有水
    }
    else  
    {
        current_status = 1;  // 干燥
    }
    
    return current_status;
}
