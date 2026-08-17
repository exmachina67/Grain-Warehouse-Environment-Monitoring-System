/**
 * @file    task_sys_monitor.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   系统监控任务 — vTaskList / CPU占用 / 堆水位，串口输出 + 投递 GUI 队列
 */
#ifndef __TASK_SYS_MONITOR_H
#define __TASK_SYS_MONITOR_H

#include "FreeRTOS.h"
#include "task.h"

/* SYS_MONITOR 任务配置 */
#define SYS_MONITOR_TASK_PRIO      2
#define SYS_MONITOR_STK_SIZE       512
#define SYS_MONITOR_PERIOD_MS      1000

void sys_monitor_task(void *pvParameters);

#endif
