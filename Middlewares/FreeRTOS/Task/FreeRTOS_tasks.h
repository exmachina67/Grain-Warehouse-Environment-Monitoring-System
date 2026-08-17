/**
 * @file    FreeRTOS_tasks.h
 * @author  010
 * @version V1.0
 * @date    2022-01-11
 * @brief   FreeRTOS 任务模块 — 所有任务的公共头文件
 */
#ifndef __FREERTOS_TASKS_H
#define __FREERTOS_TASKS_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "task.h"
#include "../Drivers/BSP/APPLAY/applay.h"

/*============================================ 跨任务通信队列 ============================================*/
extern QueueHandle_t       ActuatorCmdQueue;
extern QueueHandle_t       LoraMutex;
extern QueueHandle_t       LV_RXDataQueue;
extern QueueHandle_t       EnvSampleQueue;
extern QueueHandle_t       ReportQueue;
extern EventGroupHandle_t  SensorEventGroup;
extern SemaphoreHandle_t   FanMutex;

/*============================================ 跨任务共享状态（外部定义在 FreeOS_app.c 或 BSP 层） ============================================*/
extern volatile uint8_t mode_control;   /* 模式控制标志（1=手动 0=自动） */
extern volatile uint8_t fan_running;     /* 风扇运行状态 */
extern volatile uint8_t fan_speed;      /* 风扇速度 */
extern uint8_t         lora_ready;    /* LoRa 模块就绪标志（0=就绪 非0=未就绪） */
extern uint8_t         my_rx_buf[200]; /* LoRa 接收缓冲区 */

/*============================================ 传感器事件位 ============================================*/
#define SENSOR_DATA_READY  (1 << 0)

/*============================================ OS 层消息结构 ============================================*/

/* sensor_data_get_task 投递的"原始样本" */
typedef struct {
    uint8_t  err_bits;      /* 当前 read_error 位图 */
    uint8_t  temperature;    /* 温度 整数 ℃ */
    uint8_t  humidity;      /* 湿度 整数 % */
    uint8_t  rain_status;   /* 雨水：0=有雨 1=干 */
    uint16_t co2_value;    /* CO2 ADC 值 */
    uint16_t co_value;     /* CO  ADC 值 */
} env_sample_t;

/* data_process_task 投递的"周期平均值" */
typedef struct {
    uint8_t  err_bits;       /* 周期内最后一次 read_error 快照 */
    uint16_t temp_avg_x10;   /* 温度 ×10 */
    uint16_t humi_avg_x10;   /* 湿度 ×10 */
    uint8_t  rain_flag;      /* 0=干 1=雨（按 30% 阈值） */
    uint16_t co2_avg;        /* CO2 整数 */
    uint16_t co_avg;         /* CO  整数 */
    uint32_t sample_count;   /* 实际有效样本数 */
} env_avg_t;

/* LoRa 通用远控命令 */
typedef enum {
    LORA_TARGET_FAN   = 0,
    LORA_TARGET_HEAT  = 1,
    LORA_TARGET_HUMID = 2,
} lora_target_t;

typedef struct {
    lora_target_t target;   /* LORA_TARGET_* */
    uint8_t       command;  /* 原始字节命令值 */
    source_t      source;    /* 控制来源（固定填 SOURCE_LORA） */
} lora_cmd_t;

/*============================================ 系统资源监控 ============================================*/

typedef struct {
    char        name[16];
    UBaseType_t prio;
    UBaseType_t state;
    UBaseType_t stack_free_words;
    uint32_t    run_time_ticks;
    float       cpu_pct;
} task_stat_t;

typedef struct {
    task_stat_t tasks[16];
    uint8_t     task_count;
    uint32_t    heap_free_bytes;
    uint32_t    heap_min_free_bytes;
    uint32_t    tick_ms;
} sys_status_t;

/*============================================ 函数声明 ============================================*/

QueueHandle_t SysMonitorQueue(void);
void           SysMonitorInit(void);
void           sys_monitor_task(void *pvParameters);

#endif
