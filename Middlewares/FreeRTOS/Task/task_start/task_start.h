/**
 * @file    task_start.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   start_task 启动任务 — 系统初始化与所有任务创建
 */
#ifndef __TASK_START_H
#define __TASK_START_H

#include "FreeRTOS.h"
#include "task.h"

/* START_TASK 任务配置 */
#define START_TASK_PRIO      1
#define START_STK_SIZE       512
extern TaskHandle_t StartTask_Handler;

void start_task(void *pvParameters);

#endif
