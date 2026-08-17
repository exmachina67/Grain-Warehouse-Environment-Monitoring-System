/**
 ****************************************************************************************************
 * @file        task_lora.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       LoRa 任务 — ATK-MW1278D 收发：上行 ReportQueue 环境均值 / 下行解析控制命令
 *
 * 职责：
 *  - TX：消费 ReportQueue → 组帧 → USART3 发送（每分钟环境平均值）
 *  - RX：检查 USART3 接收帧 → 解析 FAN/HEAT/HUMID 命令 → 投 ActuatorCmdQueue
 *  - 任意时刻允许 lora_send_packet() 发命令
 *
 * 数据加工由 data_process_task 完成
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_lora/task_lora.h"
#include "FreeRTOS_tasks.h"
#include "../Drivers/BSP/APPLAY/applay.h"
#include "../../../Drivers/BSP/ATK-MW1278/atk_mw1278d.h"
#include "../../../Drivers/BSP/ATK-MW1278/atk_mw1278d_uart.h"
#include <stdio.h>
#include <string.h>

extern uint8_t lora_ready;

TaskHandle_t LoraTask_Handler;

/*============================================ 本地静态变量 ============================================*/
static volatile uint8_t g_report_seq = 0;

/*============================================ 本地工具函数 ============================================*/

/**
 * @brief  大端写入 16 位整数
 */
static inline void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/*============================================ LoRa 协议相关函数 ============================================*/

/**
 * @brief  发送一个 LoRa 数据包（任意任务可调用）
 * @param  type:  命令类型（LORA_TYPE_*）
 * @param  value: 参数
 * @retval 无
 *
 * 包格式：[HEAD][TYPE][VALUE][SUM]
 */
void lora_send_packet(uint8_t type, uint8_t value)
{
    if (lora_ready != 0) return;

    uint8_t frame[LORA_FRAME_LEN];
    frame[0] = LORA_FRAME_HEAD;
    frame[1] = type;
    frame[2] = value;
    frame[3] = (uint8_t)(frame[0] + frame[1] + frame[2]);

    if (LoraMutex != NULL) xSemaphoreTake(LoraMutex, portMAX_DELAY);
    atk_mw1278d_uart_send_bin(frame, LORA_FRAME_LEN);
    if (LoraMutex != NULL) xSemaphoreGive(LoraMutex);
}

/**
 * @brief  解析一帧 LoRa 数据，输出通用远控命令
 * @param  buf:   接收缓冲区
 * @param  len:   接收字节数
 * @param  cmd:   解析成功时填入通用命令（含 target + command）
 * @retval 1=解析成功 0=解析失败
 */
static uint8_t lora_parse_frame(uint8_t *buf, uint16_t len, lora_cmd_t *cmd)
{
    if (buf == NULL || cmd == NULL || len < LORA_FRAME_LEN) return 0;
    if (buf[0] != LORA_FRAME_HEAD) return 0;

    uint8_t sum = (uint8_t)(buf[0] + buf[1] + buf[2]);
    if (sum != buf[3]) {
        printf("[LORA] checksum err: got 0x%02X, expected 0x%02X\r\n", buf[3], sum);
        return 0;
    }

    cmd->source = SOURCE_LORA;

    switch (buf[1]) {
        case LORA_TYPE_FAN:    cmd->target = LORA_TARGET_FAN;   break;
        case LORA_TYPE_HEAT:   cmd->target = LORA_TARGET_HEAT;  break;
        case LORA_TYPE_HUMID:  cmd->target = LORA_TARGET_HUMID; break;
        case LORA_TYPE_QUERY:  return 0;
        case LORA_TYPE_ACK:   return 0;
        default:
            printf("[LORA] unknown type: 0x%02X\r\n", buf[1]);
            return 0;
    }

    cmd->command = buf[2];
    return 1;
}

/*============================================ 任务函数 ============================================*/

void lora_task(void *pvParameters)
{
    pvParameters = pvParameters;

    vTaskDelay(1000);   /* 等待屏幕和 LVGL 系统完全启动 */

    if (lora_ready != 0) {
        printf("[LORA] module not ready, lora_task exit.\r\n");
        vTaskDelete(NULL);
        return;
    }

    printf("[LORA] module ready, enter rx/tx loop.\r\n");
    atk_mw1278d_uart_rx_restart();

    for (;;)
    {
        /* ---- TX：消费 ReportQueue（环境平均值） ---- */
        if (ReportQueue != NULL) {
            env_avg_t avg;
            if (xQueueReceive(ReportQueue, &avg, 0) == pdPASS)
            {
                uint8_t frame[13];
                frame[0]  = LORA_FRAME_HEAD;
                frame[1]  = LORA_TYPE_REPORT;
                frame[2]  = g_report_seq++;
                put_be16(&frame[3],  avg.temp_avg_x10);
                put_be16(&frame[5],  avg.humi_avg_x10);
                frame[7]  = avg.rain_flag;
                put_be16(&frame[8],  avg.co2_avg);
                put_be16(&frame[10], avg.co_avg);
                frame[12] = avg.err_bits;

                if (LoraMutex != NULL) xSemaphoreTake(LoraMutex, portMAX_DELAY);
                atk_mw1278d_uart_send_bin(frame, 13);
                if (LoraMutex != NULL) xSemaphoreGive(LoraMutex);

                printf("[LORA-TX] seq=%u T=%.1fC H=%.1f%% RAIN=%u CO2=%u CO=%u err=0x%02X\r\n",
                       (unsigned)frame[2],
                       (double)avg.temp_avg_x10 / 10.0,
                       (double)avg.humi_avg_x10 / 10.0,
                       (unsigned)avg.rain_flag,
                       (unsigned)avg.co2_avg,
                       (unsigned)avg.co_avg,
                       (unsigned)avg.err_bits);
            }
        }

        /* ---- RX：检查 USART3 接收帧 ---- */
        uint16_t frame_len = atk_mw1278d_uart_rx_get_frame_len();
        if (frame_len >= LORA_FRAME_LEN) {
            uint8_t *buf = atk_mw1278d_uart_rx_get_frame();

            taskENTER_CRITICAL();
            uint8_t local_buf[LORA_FRAME_LEN];
            memcpy(local_buf, buf, LORA_FRAME_LEN);
            taskEXIT_CRITICAL();

            atk_mw1278d_uart_rx_restart();

            lora_cmd_t cmd;
            if (lora_parse_frame(local_buf, LORA_FRAME_LEN, &cmd)) {
                switch (cmd.target) {
                    case LORA_TARGET_FAN: {
                        applay_val_t fan_val;
                        switch (cmd.command) {
                            case 0: fan_val = APPLAY_VAL_OFF; break;
                            case 1: fan_val = APPLAY_VAL_LOW; break;
                            case 2: fan_val = APPLAY_VAL_MID; break;
                            case 3: fan_val = APPLAY_VAL_HI;  break;
                            default:
                                printf("[LORA-RX] unknown FAN value: %d\r\n", cmd.command);
                                goto skip_dispatch;
                        }
                        applay_msg_t am = {
                            .target = APPLAY_TARGET_FAN,
                            .value  = fan_val,
                            .source = SOURCE_LORA,
                        };
                        if (ActuatorCmdQueue == NULL ||
                            xQueueSend(ActuatorCmdQueue, &am, pdMS_TO_TICKS(100)) != pdPASS) {
                            printf("[LORA-RX] ActuatorCmdQueue full, drop FAN msg.\r\n");
                        } else {
                            const char *n[] = {"OFF","LOW","MID","HIGH"};
                            printf("[LORA-RX] FAN -> %s\r\n", n[(uint8_t)fan_val]);
                        }
                        break;
                    }

                    case LORA_TARGET_HEAT: {
                        applay_val_t heat_val = (cmd.command == 0) ? APPLAY_VAL_OFF : APPLAY_VAL_ON;
                        applay_msg_t am = {
                            .target = APPLAY_TARGET_HEAT,
                            .value  = heat_val,
                            .source = SOURCE_LORA,
                        };
                        if (ActuatorCmdQueue == NULL ||
                            xQueueSend(ActuatorCmdQueue, &am, pdMS_TO_TICKS(100)) != pdPASS) {
                            printf("[LORA-RX] ActuatorCmdQueue full, drop HEAT msg.\r\n");
                        } else {
                            printf("[LORA-RX] HEAT -> %s\r\n",
                                   (heat_val == APPLAY_VAL_OFF) ? "OFF" : "ON");
                        }
                        break;
                    }

                    case LORA_TARGET_HUMID: {
                        applay_val_t humid_val = (cmd.command == 0) ? APPLAY_VAL_OFF : APPLAY_VAL_ON;
                        applay_msg_t am = {
                            .target = APPLAY_TARGET_HUMID,
                            .value  = humid_val,
                            .source = SOURCE_LORA,
                        };
                        if (ActuatorCmdQueue == NULL ||
                            xQueueSend(ActuatorCmdQueue, &am, pdMS_TO_TICKS(100)) != pdPASS) {
                            printf("[LORA-RX] ActuatorCmdQueue full, drop HUMID msg.\r\n");
                        } else {
                            printf("[LORA-RX] HUMID -> %s\r\n",
                                   (humid_val == APPLAY_VAL_OFF) ? "OFF" : "ON");
                        }
                        break;
                    }

                    default:
                        break;
                }
                skip_dispatch:;
            }
        }

        vTaskDelay(50);
    }
}
