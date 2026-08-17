/**
 ****************************************************************************************************
 * @file        task_data_process.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       数据处理任务 — 原始样本累计 → 周期均值计算 → 投 ReportQueue/LV_RXDataQueue
 *
 * 工作职责：
 *  - 从 EnvSampleQueue 接收 sensor_data_get_task 投递的原始样本
 *  - 在 1 分钟周期内累加温度/湿度/CO2/CO/雨水
 *  - 周期到 → 求平均 → 封装成 env_avg_t → 投到 ReportQueue
 *  - 同步用 xQueueOverwrite 推给 GUI 显示
 *  - 失败传感器的字段不参与累加（保持 0），由 err_bits 通知远端
 *
 * 注意：本任务不读 USART3，只负责数据加工。LoRa 收发由 lora_task 统一负责
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_data_process/task_data_process.h"
#include "FreeRTOS_tasks.h"
#include <stdio.h>

TaskHandle_t DataProcessTask_Handler;

#define DATA_PROCESS_PERIOD_MS   (60 * 1000U)

/**
 * @brief  数据处理任务
 * @param  pvParameters: 传入参数(未用到)
 * @retval 无
 */
void data_process_task(void *pvParameters)
{
    pvParameters = pvParameters;

    uint32_t temp_sum = 0;
    uint32_t humi_sum = 0;
    uint32_t co2_sum  = 0;
    uint32_t co_sum   = 0;
    uint32_t rain_sum = 0;     /* 1=有雨 0=干 */
    uint32_t cnt      = 0;
    uint8_t  last_err = 0;

    TickType_t period_start = xTaskGetTickCount();

    for (;;)
    {
        /* ---- 阻塞接收一个样本（最长 1 分钟） ---- */
        env_sample_t sample;
        if (EnvSampleQueue != NULL)
        {
            if (xQueueReceive(EnvSampleQueue, &sample, pdMS_TO_TICKS(DATA_PROCESS_PERIOD_MS)) == pdPASS)
            {
                if (!(sample.err_bits & (1 << 0))) {
                    temp_sum += (uint32_t)sample.temperature * 10U;
                    humi_sum += (uint32_t)sample.humidity    * 10U;
                }
                if (!(sample.err_bits & (1 << 1))) {
                    rain_sum += sample.rain_status ? 0U : 1U;
                }
                if (!(sample.err_bits & (1 << 3))) {
                    co2_sum += sample.co2_value;
                }
                if (!(sample.err_bits & (1 << 4))) {
                    co_sum  += sample.co_value;
                }
                cnt++;
                last_err = sample.err_bits;
            } else {
                printf("[DATA-PROC] queue receive timeout (no sample in 1 min), cnt=%lu\r\n",
                       (unsigned long)cnt);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ---- 1 分钟到：求平均并投递 ---- */
        TickType_t now = xTaskGetTickCount();
        if ((now - period_start) < pdMS_TO_TICKS(DATA_PROCESS_PERIOD_MS)) {
            continue;
        }

        if (cnt == 0) {
            printf("[DATA-PROC] no samples this period, skip.\r\n");
        } else {
            env_avg_t avg;
            avg.err_bits     = last_err;
            avg.temp_avg_x10 = (uint16_t)(temp_sum / cnt);
            avg.humi_avg_x10 = (uint16_t)(humi_sum / cnt);
            avg.co2_avg      = (uint16_t)(co2_sum  / cnt);
            avg.co_avg       = (uint16_t)(co_sum   / cnt);
            avg.sample_count = cnt;
            avg.rain_flag    = ((rain_sum * 10U) >= (cnt * 3U)) ? 1U : 0U;

            if (ReportQueue != NULL) {
                if (xQueueSend(ReportQueue, &avg, pdMS_TO_TICKS(100)) != pdPASS) {
                    printf("[DATA-PROC] report queue full, drop 1 period.\r\n");
                } else {
                    printf("[DATA-PROC] avg ready: T=%.1fC H=%.1f%% RAIN=%u CO2=%u CO=%u err=0x%02X N=%lu\r\n",
                           (double)avg.temp_avg_x10 / 10.0,
                           (double)avg.humi_avg_x10 / 10.0,
                           (unsigned)avg.rain_flag,
                           (unsigned)avg.co2_avg,
                           (unsigned)avg.co_avg,
                           (unsigned)avg.err_bits,
                           (unsigned long)cnt);
                }
            }

            /* 同步把均值推给 GUI */
            if (LV_RXDataQueue != NULL) {
                xQueueOverwrite(LV_RXDataQueue, &avg);
            }
        }

        /* ---- 清零累积器 + 重置周期起点 ---- */
        temp_sum  = 0;
        humi_sum  = 0;
        co2_sum   = 0;
        co_sum    = 0;
        rain_sum  = 0;
        cnt       = 0;
        period_start = now;
    }
}
