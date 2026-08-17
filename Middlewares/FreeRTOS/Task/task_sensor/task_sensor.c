/**
 ****************************************************************************************************
 * @file        task_sensor.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       传感器任务 — DHT11 / FC37 / MQ135 / MQ7 数据采集与自动控制
 *
 * 工作职责：
 *  - 500ms 周期读取所有传感器数据
 *  - 传感器异常时保留上次有效值
 *  - 自动模式：依据传感器状态通过 ActuatorCmdQueue 控制风扇档位
 *  - 手动模式超时 30 分钟自动切回自动模式
 *  - 原始样本投 EnvSampleQueue → data_process_task
 *  - 边沿触发打印传感器错误/恢复状态
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_sensor/task_sensor.h"
#include "FreeRTOS_tasks.h"
#include "../../../Drivers/BSP/DHT11/dht11.h"
#include "../../../Drivers/BSP/FC37/fc37.h"
#include "../../../Drivers/BSP/MQ135/mq135.h"
#include "../../../Drivers/BSP/MQ7/mq7.h"
#include "../../../Drivers/BSP/BEEP/beep.h"
#include <stdio.h>

TaskHandle_t SENSOR_DATA_GET_Handler;

/*============================================ 本地宏 ============================================*/
#define MODE_TIMEOUT_MS      (30 * 60 * 1000U)
#define CO2_SAFE_THRESHOLD  1000
#define CO_SAFE_THRESHOLD   200

/* 传感器错误位定义 */
#define SENSOR_ERR_DHT11    (1 << 0)
#define SENSOR_ERR_FC37     (1 << 1)
#define SENSOR_ERR_MQ135    (1 << 3)
#define SENSOR_ERR_MQ7      (1 << 4)

/*============================================ 本地全局变量 ============================================*/
static uint8_t  read_error = 0;
static uint16_t last_valid_rain = 0;
static uint16_t last_valid_co2 = 0;
static uint16_t last_valid_co  = 0;

/*============================================ 本地函数 ============================================*/

/**
 * @brief  打印传感器错误状态（边沿触发：只在变化时打印）
 */
static void print_sensor_error_state(uint8_t cur, uint8_t *last)
{
    if (cur == *last) return;

    taskENTER_CRITICAL();

    if ((cur & SENSOR_ERR_DHT11) && !(*last & SENSOR_ERR_DHT11)) {
        printf("[SENSOR ERR] DHT11 (T&H) read failed!\r\n");
    } else if (!(cur & SENSOR_ERR_DHT11) && (*last & SENSOR_ERR_DHT11)) {
        printf("[SENSOR OK ] DHT11 (T&H) read recovered\r\n");
    }

    if ((cur & SENSOR_ERR_FC37) && !(*last & SENSOR_ERR_FC37)) {
        printf("[SENSOR ERR] FC37 (rain) read failed!\r\n");
    } else if (!(cur & SENSOR_ERR_FC37) && (*last & SENSOR_ERR_FC37)) {
        printf("[SENSOR OK ] FC37 (rain) read recovered\r\n");
    }

    if ((cur & SENSOR_ERR_MQ135) && !(*last & SENSOR_ERR_MQ135)) {
        printf("[SENSOR ERR] MQ135 (CO2) read failed!\r\n");
    } else if (!(cur & SENSOR_ERR_MQ135) && (*last & SENSOR_ERR_MQ135)) {
        printf("[SENSOR OK ] MQ135 (CO2) read recovered\r\n");
    }

    if ((cur & SENSOR_ERR_MQ7) && !(*last & SENSOR_ERR_MQ7)) {
        printf("[SENSOR ERR] MQ7 (CO) read failed!\r\n");
    } else if (!(cur & SENSOR_ERR_MQ7) && (*last & SENSOR_ERR_MQ7)) {
        printf("[SENSOR OK ] MQ7 (CO) read recovered\r\n");
    }

    printf("[read_error] 0x%02X (bit0=DHT11 bit1=FC37 bit3=MQ135 bit4=MQ7)\r\n", cur);

    taskEXIT_CRITICAL();

    *last = cur;
}

/*============================================ 任务函数 ============================================*/

void sensor_data_get_task(void *pvParameters)
{
    pvParameters = pvParameters;

    static uint8_t last_read_error = 0xFF;

    while (1)
    {
        /* ---- DHT11 温湿度 ---- */
        uint8_t temperature = 0;
        uint8_t humidity    = 0;
        uint8_t ret = DHT11_Read_Data(&temperature, &humidity);
        if (ret == 0 && temperature > 0 && temperature < 50 &&
            humidity > 0 && humidity < 100) {
            read_error &= ~(1 << 0);
        } else {
            read_error |= (1 << 0);
        }

        /* ---- FC37 雨量 ---- */
        uint16_t rain_value  = FC37_ReadAO();
        uint8_t  rain_status = FC37_GetStatus();
        if (rain_value > 0 && rain_value < 4096) {
            last_valid_rain = rain_value;
            read_error &= ~(1 << 1);
        } else {
            rain_value = last_valid_rain;
            read_error |= (1 << 1);
        }

        /* ---- MQ135 CO2 ---- */
        uint16_t co2_value = MQ135_ReadCO2();
        if (co2_value > 0 && co2_value < 4096) {
            last_valid_co2 = co2_value;
            read_error &= ~(1 << 3);
        } else {
            co2_value = last_valid_co2;
            read_error |= (1 << 3);
        }

        /* ---- MQ7 CO ---- */
        uint16_t co_value = MQ7_ReadAO();
        uint8_t  co_status = !MQ7_GetStatus();
        if (co_value > 0 && co_value < 4096) {
            last_valid_co = co_value;
            read_error &= ~(1 << 4);
        } else {
            co_value = last_valid_co;
            read_error |= (1 << 4);
        }

        /* ---- 报警判断 + 自动风扇档位决策 ---- */
        applay_val_t auto_cmd = APPLAY_VAL_OFF;

        if (!(read_error & (1 << 3)) && co2_value > CO2_SAFE_THRESHOLD) {
            auto_cmd   = APPLAY_VAL_HI;
        } else if (!(read_error & (1 << 4)) && co_value > CO_SAFE_THRESHOLD) {
            auto_cmd   = APPLAY_VAL_HI;
        } else if (!(read_error & (1 << 0)) && humidity > 60) {
            auto_cmd   = APPLAY_VAL_MID;
        } else if (!(read_error & (1 << 1)) && (!rain_status)) {
            auto_cmd   = APPLAY_VAL_LOW;
        } else {
            BEEP(0);
            auto_cmd   = APPLAY_VAL_OFF;
        }

        /* ---- 自动控制：发送命令到 ActuatorCmdQueue ---- */
        static applay_val_t last_auto_cmd = (applay_val_t)-1;
        if (auto_cmd != last_auto_cmd) {
            if (mode_control == 0) {   /* 仅在自动模式下发送 */
                applay_msg_t am = {
                    .target = APPLAY_TARGET_FAN,
                    .value  = auto_cmd,
                    .source = SOURCE_AUTO,
                };
                if (ActuatorCmdQueue == NULL ||
                    xQueueSend(ActuatorCmdQueue, &am, 0) != pdPASS) {
                    printf("[AUTO] ActuatorCmdQueue full, drop FAN auto cmd\r\n");
                }
            }
            last_auto_cmd = auto_cmd;
        }

        /* ---- 设置传感器数据就绪事件 ---- */
        xEventGroupSetBits(SensorEventGroup, SENSOR_DATA_READY);

        /* ---- 投递原始样本到 EnvSampleQueue ---- */
        if (EnvSampleQueue != NULL) {
            env_sample_t sample;
            sample.err_bits    = read_error;
            sample.temperature = temperature;
            sample.humidity    = humidity;
            sample.rain_status = rain_status;
            sample.co2_value  = co2_value;
            sample.co_value   = co_value;

            static uint8_t last_was_full = 0;
            BaseType_t ok = xQueueSend(EnvSampleQueue, &sample, 0);
            if (ok != pdPASS) {
                if (!last_was_full) {
                    printf("[SENSOR] EnvSampleQueue full, dropping sample\r\n");
                    last_was_full = 1;
                }
            } else {
                last_was_full = 0;
            }
        }

        /* ---- 打印传感器错误状态（边沿触发） ---- */
        print_sensor_error_state(read_error, &last_read_error);

        /* ---- 手动模式超时回自动 ---- */
        if (mode_control == 1) {
            static TickType_t manual_enter_tick = 0;
            static uint8_t prev_mode_control = 0;
            if (prev_mode_control == 0) {
                manual_enter_tick = xTaskGetTickCount();
            }
            prev_mode_control = 1;

            TickType_t now = xTaskGetTickCount();
            if ((now - manual_enter_tick) >= pdMS_TO_TICKS(MODE_TIMEOUT_MS)) {
                mode_control = 0;
                prev_mode_control = 0;
            }
        }

        vTaskDelay(500);
    }
}
