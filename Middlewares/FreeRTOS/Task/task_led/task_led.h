/**
 * @file    task_led.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   LED 任务 — 板载 LED 翻转指示
 */
#ifndef __TASK_LED_H
#define __TASK_LED_H

#include "FreeRTOS.h"
#include "task.h"

/* LED_TASK 任务配置 */
#define LED_TASK_PRIO        2
#define LED_STK_SIZE         128
extern TaskHandle_t LEDTask_Handler;

void led_task(void *pvParameters);

#endif
