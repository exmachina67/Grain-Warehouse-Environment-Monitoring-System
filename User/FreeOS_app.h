/**
 * @file    FreeOS_app.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   FreeRTOS 应用层头文件 — 公共声明
 */
#ifndef __FREERTOS_APP_H
#define __FREERTOS_APP_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "task.h"
#include "../Drivers/BSP/APPLAY/applay.h"
#include "FreeRTOS_tasks.h"
#include "FreeRtos/Task/task_gui/task_gui.h"

/*============================================ 跨任务通信队列/信号量（外部定义） ============================================*/
extern QueueHandle_t       ActuatorCmdQueue;
extern QueueHandle_t       LoraMutex;
extern QueueHandle_t       LV_RXDataQueue;
extern SemaphoreHandle_t   FanMutex;
extern EventGroupHandle_t  SensorEventGroup;
extern QueueHandle_t       EnvSampleQueue;
extern QueueHandle_t       ReportQueue;

/*============================================ 全局状态变量（外部定义） ============================================*/
extern volatile uint8_t mode_control;    /* 模式控制标志（1=手动 0=自动） */
extern volatile uint8_t fan_running;     /* 风扇运行状态 */
extern volatile uint8_t fan_speed;      /* 风扇速度 */
extern uint8_t          lora_ready;    /* LoRa 模块就绪标志 */
extern uint8_t          my_rx_buf[200]; /* LoRa 接收缓冲区 */

/*============================================ 传感器事件 ============================================*/
#define SENSOR_DATA_READY  (1 << 0)

/*============================================ 函数声明 ============================================*/
void FreeOS_app(void);

/* GUI 任务静态堆栈（定义在 FreeOS_app.c，供 start_task 引用） */
extern StackType_t lvgl_stack[MY_GUI_STK_SIZE];
extern StaticTask_t lvgl_tcb;

/* 系统监控 */
QueueHandle_t SysMonitorQueue(void);
void            SysMonitorInit(void);

#endif
