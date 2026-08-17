/**
 * @file    task_sensor.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   传感器任务 — DHT11 / FC37 / MQ135 / MQ7 数据采集与自动控制
 */
#ifndef __TASK_SENSOR_H
#define __TASK_SENSOR_H

#include "FreeRTOS.h"
#include "task.h"

/* sensor_task 任务配置 */
#define SENSOR_DATA_GET_TASK_PRIO   3
#define SENSOR_DATA_GET_STK_SIZE    512
extern TaskHandle_t SENSOR_DATA_GET_Handler;

void sensor_data_get_task(void *pvParameters);

#endif
