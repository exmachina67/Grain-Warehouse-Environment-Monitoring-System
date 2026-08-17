#include "stm32f4xx.h"                  // Device header

#include "./BSP/LIGHT/light.h"

/**
 * @brief   初始化三路指示灯 GPIO
 */
void light_init(void)
{
    GPIO_InitTypeDef GPIO_Initure;

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_Initure.Pin    = fan_light;
    GPIO_Initure.Mode   = GPIO_MODE_OUTPUT_PP;
    GPIO_Initure.Pull   = GPIO_PULLUP;
    GPIO_Initure.Speed  = GPIO_SPEED_HIGH;

    HAL_GPIO_Init(GPIOC, &GPIO_Initure);

    GPIO_Initure.Pin    = hot_light;
    HAL_GPIO_Init(GPIOC, &GPIO_Initure);

    GPIO_Initure.Pin    = water_light;
    HAL_GPIO_Init(GPIOC, &GPIO_Initure);
}

/**
 * @brief   内部辅助：逐路关闭指定通道的指示灯
 */
void light_off(light_id_t id)
{
    switch (id)
    {
        case LIGHT_FAN:
            HAL_GPIO_WritePin(GPIOC, fan_light, GPIO_PIN_RESET);
            break;

        case LIGHT_HEAT:
            HAL_GPIO_WritePin(GPIOC, hot_light, GPIO_PIN_RESET);
            break;

        case LIGHT_HUMID:
            HAL_GPIO_WritePin(GPIOC, water_light, GPIO_PIN_RESET);
            break;

        case LIGHT_NONE:
            HAL_GPIO_WritePin(GPIOC, fan_light | hot_light |water_light, GPIO_PIN_RESET);
            break;
        default:
            break;
    }
}

/**
 * @brief   点亮指定通道的指示灯，并逐路熄灭其它通道
 * @param   id : light_id_t 枚举值，如 LIGHT_FAN / LIGHT_HEAT / LIGHT_HUMID
 */
void light_on(light_id_t id)
{

    switch (id)
    {
        case LIGHT_FAN:                 /* 风扇指示灯 */
            HAL_GPIO_WritePin(GPIOC, fan_light, GPIO_PIN_SET);
            break;

        case LIGHT_HEAT:                /* 加热指示灯 */
            HAL_GPIO_WritePin(GPIOC, hot_light, GPIO_PIN_SET);
            break;

        case LIGHT_HUMID:               /* 加湿指示灯 */
            HAL_GPIO_WritePin(GPIOC, water_light, GPIO_PIN_SET);
            break;

        case LIGHT_NONE:
        default:
            /* 不点亮任何灯，保持全灭 */
            break;
    }
}


