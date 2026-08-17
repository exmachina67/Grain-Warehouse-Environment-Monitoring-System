/**
 * @file    task_data_process.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   数据处理任务 — 原始样本累计 → 周期均值计算 → 投 ReportQueue/LV_RXDataQueue
 */
#ifndef __TASK_DATA_PROCESS_H
#define __TASK_DATA_PROCESS_H

#include "FreeRTOS.h"
#include "task.h"

/* DATA_PROCESS_TASK 任务配置 */
#define DATA_PROCESS_TASK_PRIO   5
#define DATA_PROCESS_STK_SIZE    256
extern TaskHandle_t DataProcessTask_Handler;

void data_process_task(void *pvParameters);

#endif
