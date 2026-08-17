/**
 * @file    task_watchdog.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   看门狗任务 — 独立 IWDG 定时喂狗，保证系统健壮性
 */
#ifndef __TASK_WATCHDOG_H
#define __TASK_WATCHDOG_H

#include "FreeRTOS.h"
#include "task.h"

/* WATCHDOG_TASK 任务配置 */
#define WATCHDOG_TASK_PRIO        8
#define WATCHDOG_STK_SIZE         128
#define WATCHDOG_FEED_PERIOD_MS   (10 * 1000U)
extern TaskHandle_t WatchdogTask_Handler;

void watchdog_feed_task(void *pvParameters);

#endif
