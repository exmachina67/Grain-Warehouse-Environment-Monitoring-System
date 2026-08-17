/**
 ****************************************************************************************************
 * @file        task_start.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       启动任务 — 系统初始化与所有任务创建
 *
 * 负责：
 *  - 创建所有通信队列、信号量、事件组
 *  - 创建所有业务任务（除 start 本身外）
 *  - 创建完成后自删除
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_start/task_start.h"
#include "FreeRTOS_tasks.h"
#include "./task_led/task_led.h"
#include "./task_sensor/task_sensor.h"
#include "./task_gui/task_gui.h"
#include "./task_applay/task_applay.h"
#include "./task_lora/task_lora.h"
#include "./task_data_process/task_data_process.h"
#include "./task_mqtt/task_mqtt.h"
#include "./task_watchdog/task_watchdog.h"
#include "./task_sys_monitor/task_sys_monitor.h"
#include "../../../Drivers/BSP/ESP8266/esp8266.h"
#include "../../../Drivers/BSP/MQTT/mqtt.h"

TaskHandle_t StartTask_Handler;

/*============================================ 队列和信号量定义 ============================================*/
QueueHandle_t       ActuatorCmdQueue   = NULL;
QueueHandle_t       LoraMutex          = NULL;
QueueHandle_t       LV_RXDataQueue     = NULL;
SemaphoreHandle_t   FanMutex           = NULL;
EventGroupHandle_t  SensorEventGroup   = NULL;
QueueHandle_t       EnvSampleQueue     = NULL;
QueueHandle_t       ReportQueue        = NULL;

/*============================================ 任务函数 ============================================*/

/**
 * @brief  启动任务 — 创建所有队列和任务后自删除
 * @param  pvParameters: 传入参数(未用到)
 * @retval 无
 */
void start_task(void *pvParameters)
{
    pvParameters = pvParameters;

    taskENTER_CRITICAL();

    /* ---- 通信原语创建 ---- */
    SensorEventGroup = xEventGroupCreate();
    configASSERT(SensorEventGroup != NULL);

    FanMutex = xSemaphoreCreateMutex();
    configASSERT(FanMutex != NULL);

    ActuatorCmdQueue = xQueueCreate(10, sizeof(applay_msg_t));
    configASSERT(ActuatorCmdQueue != NULL);

    LoraMutex = xSemaphoreCreateMutex();
    configASSERT(LoraMutex != NULL);

    EnvSampleQueue = xQueueCreate(8, sizeof(env_sample_t));
    configASSERT(EnvSampleQueue != NULL);

    ReportQueue = xQueueCreate(2, sizeof(env_avg_t));
    configASSERT(ReportQueue != NULL);

    LV_RXDataQueue = xQueueCreate(1, sizeof(env_avg_t));
    configASSERT(LV_RXDataQueue != NULL);

    /* ---- 创建业务任务 ---- */

    /* LED 提示任务 */
    xTaskCreate((TaskFunction_t )led_task,
                (const char*    )"led_task",
                (uint16_t       )LED_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )LED_TASK_PRIO,
                (TaskHandle_t*  )&LEDTask_Handler);

    /* 系统资源监控任务 */
    SysMonitorInit();

    /* 传感器数据获取任务 */
    xTaskCreate((TaskFunction_t )sensor_data_get_task,
                (const char*    )"sensoe_data_get_task",
                (uint16_t       )SENSOR_DATA_GET_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )SENSOR_DATA_GET_TASK_PRIO,
                (TaskHandle_t*  )&SENSOR_DATA_GET_Handler);

    /* LVGL GUI 任务（静态堆栈） */
    extern StackType_t lvgl_stack[MY_GUI_STK_SIZE];
    extern StaticTask_t lvgl_tcb;
    MY_GUI_Handler = xTaskCreateStatic((TaskFunction_t )my_gui_task,
                      (const char*    )"my_gui_task",
                      (uint16_t       )MY_GUI_STK_SIZE,
                      (void*          )NULL,
                      (UBaseType_t    )MY_GUI_TASK_PRIO,
                      (StackType_t*   )lvgl_stack,
                      (StaticTask_t*  )&lvgl_tcb);

    /* 执行器任务 */
    xTaskCreate((TaskFunction_t )applay_task,
                (const char*    )"applay_task",
                (uint16_t       )APPLAY_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )APPLAY_TASK_PRIO,
                (TaskHandle_t*  )&ApplayTask_Handler);

    /* LoRa 任务 */
    xTaskCreate((TaskFunction_t )lora_task,
                (const char*    )"lora_task",
                (uint16_t       )LORA_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )LORA_TASK_PRIO,
                (TaskHandle_t*  )&LoraTask_Handler);

    /* 数据处理任务 */
    xTaskCreate((TaskFunction_t )data_process_task,
                (const char*    )"data_process_task",
                (uint16_t       )DATA_PROCESS_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )DATA_PROCESS_TASK_PRIO,
                (TaskHandle_t*  )&DataProcessTask_Handler);

    /* MQTT 任务 */
    xTaskCreate((TaskFunction_t )mqtt_task,
                (const char*    )"mqtt_task",
                (uint16_t       )MQTT_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )MQTT_TASK_PRIO,
                (TaskHandle_t*  )&MqttTask_Handler);

    /* 看门狗喂狗任务 */
    xTaskCreate((TaskFunction_t )watchdog_feed_task,
                (const char*    )"watchdog_task",
                (uint16_t       )WATCHDOG_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )WATCHDOG_TASK_PRIO,
                (TaskHandle_t*  )&WatchdogTask_Handler);

    taskEXIT_CRITICAL();

    vTaskDelete(StartTask_Handler);
}
