/**
 * @file    task_gui.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   GUI 任务 — LVGL 界面显示与数据泵送
 */
#ifndef __TASK_GUI_H
#define __TASK_GUI_H

#include "FreeRTOS.h"
#include "task.h"

/* MY_GUI_TASK 任务配置 */
#define MY_GUI_TASK_PRIO   4
#define MY_GUI_STK_SIZE    512
extern TaskHandle_t MY_GUI_Handler;

void my_gui_task(void *pvParameters);

#endif
