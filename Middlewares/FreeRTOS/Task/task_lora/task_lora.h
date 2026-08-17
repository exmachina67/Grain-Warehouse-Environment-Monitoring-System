/**
 * @file    task_lora.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   LoRa 任务 — ATK-MW1278D 收发：上行 ReportQueue 环境均值 / 下行解析控制命令
 */
#ifndef __TASK_LORA_H
#define __TASK_LORA_H

#include "FreeRTOS.h"
#include "task.h"

/* LORA_TASK 任务配置 */
#define LORA_TASK_PRIO      7
#define LORA_STK_SIZE       256
extern TaskHandle_t LoraTask_Handler;

/* LoRa 协议帧定义 */
#define LORA_FRAME_HEAD     0xAA
#define LORA_FRAME_LEN      4

#define LORA_TYPE_FAN       0x01
#define LORA_TYPE_HEAT      0x02
#define LORA_TYPE_HUMID     0x03
#define LORA_TYPE_QUERY     0x10
#define LORA_TYPE_ACK       0xF0
#define LORA_TYPE_REPORT    0x20

void lora_send_packet(uint8_t type, uint8_t value);

void lora_task(void *pvParameters);

#endif
