/**
 ****************************************************************************************************
 * @file        task_watchdog.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       看门狗任务 — 独立 IWDG 定时喂狗
 *
 * 工作职责：
 *  - 每 10s 喂一次独立看门狗（IWDG）
 *  - IWDG 超时设为 20s（main.c 中配置）
 *  - 安全裕量 10s：允许瞬时阻塞，但若系统整体卡死超过 20s → 自动复位
 *  - 优先级设为最高（8），确保本任务在系统繁忙时也能按时执行
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_watchdog/task_watchdog.h"
#include "../../../Drivers/BSP/WDG/wdg.h"
#include <stdio.h>

TaskHandle_t WatchdogTask_Handler;

/**
 * @brief  看门狗喂狗任务
 * @param  pvParameters: 传入参数(未用到)
 * @retval 无
 */
void watchdog_feed_task(void *pvParameters)
{
    pvParameters = pvParameters;

    TickType_t last_wake = xTaskGetTickCount();

    while (1)
    {
        iwdg_feed();
        printf("[WDG] feed dog @%lu ms\r\n", (unsigned long)xTaskGetTickCount());
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(WATCHDOG_FEED_PERIOD_MS));
    }
}
