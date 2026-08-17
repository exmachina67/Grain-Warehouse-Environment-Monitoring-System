#include "./BSP/ST021/st021.h"

/**
 * @brief       加热初始化
 */
void ST021_Init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio_init_struct.Pin = GPIO_PIN_2;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull = GPIO_PULLDOWN;
    gpio_init_struct.Speed = GPIO_SPEED_HIGH;

    HAL_GPIO_Init(GPIOC, &gpio_init_struct);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
}

/**
 * @brief       加热控制
 * @param       a: 1-打开, 0-关闭
 */
void ST021_CON(int a)
{
    if(a == 1)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
    }
    else if(a == 0)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
    }
}


