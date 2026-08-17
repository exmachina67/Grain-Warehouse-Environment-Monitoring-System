/**
 ****************************************************************************************************
 * @file        task_applay.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       执行器任务 — 消费 ActuatorCmdQueue，统一调度风扇/加热/加湿
 *
 * 职责：
 *  - 从 ActuatorCmdQueue 取出 applay_msg_t → applay_dispatch()
 *  - applay_dispatch 内部按 msg->target 路由到 fan/heat/humid_apply()
 *  - *_apply() 内部按 msg->source 决定 msg->value 的语义
 *
 * 实现：每次循环里用 0 ticks（非阻塞）尝试收 1 条；没活则 vTaskDelay(10) 让出 CPU
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_applay/task_applay.h"
#include "FreeRTOS_tasks.h"
#include "../Drivers/BSP/APPLAY/applay.h"

TaskHandle_t ApplayTask_Handler;

/**
 * @brief  执行器任务
 * @param  pvParameters: 传入参数(未用到)
 * @retval 无
 */
void applay_task(void *pvParameters)
{
    pvParameters = pvParameters;

    applay_msg_t am;

    while (1)
    {
        if (ActuatorCmdQueue != NULL &&
            xQueueReceive(ActuatorCmdQueue, &am, 0) == pdPASS)
        {
            applay_dispatch(&am);
        }
        else
        {
            vTaskDelay(10);   /* 没活就睡一下，避免活循环占 CPU */
        }
    }
}
