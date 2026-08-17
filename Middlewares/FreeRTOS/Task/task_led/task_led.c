/**
 ****************************************************************************************************
 * @file        task_led.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       LED 任务 — 板载 LED 翻转指示
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_led/task_led.h"
#include "../../../Drivers/BSP/LED/led.h"

TaskHandle_t LEDTask_Handler;

/**
 * @brief  LED 任务函数
 * @param  pvParameters: 传入参数(未用到)
 * @retval 无
 */
void led_task(void *pvParameters)
{
    pvParameters = pvParameters;

    while (1)
    {
        LED0_TOGGLE();
        vTaskDelay(1000);
    }
}
