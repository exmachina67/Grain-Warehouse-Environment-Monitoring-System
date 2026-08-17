/**
 * @file    task_mqtt.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   MQTT 任务 — ESP8266 + MQTT：上行 ReportQueue JSON 推送 / 下行解析分发控制
 */
#ifndef __TASK_MQTT_H
#define __TASK_MQTT_H

#include "FreeRTOS.h"
#include "task.h"

/* MQTT_TASK 任务配置 */
#define MQTT_TASK_PRIO           5
#define MQTT_STK_SIZE            512
extern TaskHandle_t MqttTask_Handler;

void mqtt_task(void *pvParameters);

#endif
