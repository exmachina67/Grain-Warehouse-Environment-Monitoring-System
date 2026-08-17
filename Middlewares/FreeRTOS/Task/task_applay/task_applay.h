/**
 * @file    task_applay.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   执行器任务 — 消费 ActuatorCmdQueue，统一调度风扇/加热/加湿
 */
#ifndef __TASK_APPLAY_H
#define __TASK_APPLAY_H

#include "FreeRTOS.h"
#include "task.h"

/* APPLAY_TASK 任务配置 */
#define APPLAY_TASK_PRIO   6
#define APPLAY_STK_SIZE    256
extern TaskHandle_t ApplayTask_Handler;

void applay_task(void *pvParameters);

#endif
