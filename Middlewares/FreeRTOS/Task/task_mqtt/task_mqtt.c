/**
 ****************************************************************************************************
 * @file        task_mqtt.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       MQTT 任务 — ESP8266 + MQTT：上行 ReportQueue JSON 推送 / 下行解析分发控制
 *
 * 职责（与 lora_task 同构）：
 *  - TX：消费 ReportQueue → 构造 JSON 文本 → mqtt_publish() 上报到 "device/report"
 *  - RX：收到 "device/ctrl" 下行消息 → 解析 target/value → 投 ActuatorCmdQueue
 *         source = SOURCE_NETWORK
 *  - 网络层：周期性 mqtt_loop() 处理心跳/下行分发；维护 WiFi+MQTT 重连
 *
 * 下行 JSON：
 *   {"target":"fan","value":2}    → FAN MID 档
 *   {"target":"heat","value":1}   → HEAT 开
 *   {"target":"humid","value":0}  → HUMID 关
 *
 * 上行 JSON（device/report）：
 *   {"t":25.3,"h":60.1,"rain":0,"co2":820,"co":12,"err":0}
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_mqtt/task_mqtt.h"
#include "FreeRTOS_tasks.h"
#include "../Drivers/BSP/APPLAY/applay.h"
#include "../../../Drivers/BSP/ESP8266/esp8266.h"
#include "../../../Drivers/BSP/MQTT/mqtt.h"
#include <stdio.h>
#include <string.h>

TaskHandle_t MqttTask_Handler;

/*============================================ MQTT 配置 ============================================*/
/* ESP8266 直接用代码里的 SSID/PWD 连 WiFi */
#define MQTT_WIFI_SSID       "<YOUR_WIFI_SSID>"
#define MQTT_WIFI_PASS       "<YOUR_WIFI_PASS>"
#define MQTT_BROKER_HOST     "broker-cn.emqx.io"
#define MQTT_BROKER_PORT     1883
#define MQTT_CLIENT_ID       "stm32f407_agro_001"
#define MQTT_USER            ""
#define MQTT_PASS            ""
#define MQTT_KEEPALIVE_SEC   10

#define MQTT_TOPIC_REPORT    "device/report"
#define MQTT_TOPIC_CTRL      "device/ctrl"
#define MQTT_REPORT_BUF_SIZE 192

/*============================================ 本地工具函数 ============================================*/

/**
 * @brief  JSON 中按 key 取数字（首个数字串）
 */
static int32_t json_find_int(const char *json, const char *key)
{
    if (json == NULL || key == NULL) return -1;
    char needle[32];
    int n = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return -1;
    const char *p = strstr(json, needle);
    if (p == NULL) return -1;
    p += n;
    while (*p == ' ' || *p == '\t') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p < '0' || *p > '9') return -1;
    int32_t val = 0;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    return neg ? -val : val;
}

/**
 * @brief  JSON 中按 key 取字符串值
 */
static int json_find_str(const char *json, const char *key, char *out, size_t out_size)
{
    if (json == NULL || key == NULL || out == NULL || out_size == 0) return -1;
    char needle[32];
    int n = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return -1;
    const char *p = strstr(json, needle);
    if (p == NULL) return -1;
    p += n;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) { out[i++] = *p++; }
    out[i] = '\0';
    return (int)i;
}

/*============================================ MQTT 下行回调 ============================================*/

/**
 * @brief  MQTT 下行回调（运行在 mqtt_loop() 上下文里）
 */
static void mqtt_on_message(const mqtt_message_t *msg)
{
    if (msg == NULL || msg->payload == NULL || msg->payload_len == 0) return;

    char json[128];
    uint16_t copy_len = (msg->payload_len < sizeof(json) - 1) ? msg->payload_len : (uint16_t)(sizeof(json) - 1);
    memcpy(json, msg->payload, copy_len);
    json[copy_len] = '\0';

    char target_str[16] = {0};
    if (json_find_str(json, "target", target_str, sizeof(target_str)) <= 0) {
        printf("[MQTT-RX] bad cmd (no target): %s\r\n", json);
        return;
    }
    int32_t value = json_find_int(json, "value");
    if (value < 0) {
        printf("[MQTT-RX] bad cmd (no value): %s\r\n", json);
        return;
    }

    applay_msg_t am;
    am.value  = APPLAY_VAL_OFF;
    am.source = SOURCE_NETWORK;

    if (strcmp(target_str, "fan") == 0) {
        am.target = APPLAY_TARGET_FAN;
        switch (value) {
            case 0: am.value = APPLAY_VAL_OFF; break;
            case 1: am.value = APPLAY_VAL_LOW; break;
            case 2: am.value = APPLAY_VAL_MID; break;
            case 3: am.value = APPLAY_VAL_HI;  break;
            default:
                printf("[MQTT-RX] unknown fan value: %ld\r\n", (long)value);
                return;
        }
    } else if (strcmp(target_str, "heat") == 0) {
        am.target = APPLAY_TARGET_HEAT;
        am.value  = (value == 0) ? APPLAY_VAL_OFF : APPLAY_VAL_ON;
    } else if (strcmp(target_str, "humid") == 0) {
        am.target = APPLAY_TARGET_HUMID;
        am.value  = (value == 0) ? APPLAY_VAL_OFF : APPLAY_VAL_ON;
    } else {
        printf("[MQTT-RX] unknown target: %s\r\n", target_str);
        return;
    }

    if (ActuatorCmdQueue == NULL ||
        xQueueSend(ActuatorCmdQueue, &am, pdMS_TO_TICKS(100)) != pdPASS) {
        printf("[MQTT-RX] ActuatorCmdQueue full, drop msg.\r\n");
        return;
    }

    if (am.target == APPLAY_TARGET_FAN) {
        const char *n[] = {"OFF","LOW","MID","HIGH"};
        printf("[MQTT-RX] FAN -> %s\r\n", n[(uint8_t)am.value]);
    } else {
        const char *tn = (am.target == APPLAY_TARGET_HEAT) ? "HEAT" : "HUMID";
        printf("[MQTT-RX] %s -> %s\r\n", tn, (am.value == APPLAY_VAL_OFF) ? "OFF" : "ON");
    }
}

/*============================================ 任务函数 ============================================*/

typedef enum {
    MQTT_PHASE_INIT = 0,
    MQTT_PHASE_MQTT_INIT,
    MQTT_PHASE_CONNECT,
    MQTT_PHASE_SUB,
    MQTT_PHASE_RUN,
} mqtt_phase_t;

void mqtt_task(void *pvParameters)
{
    pvParameters = pvParameters;

    vTaskDelay(2000);   /* 等系统稳定 */

    mqtt_set_message_cb(mqtt_on_message);

    if (esp8266_init() != ESP_OK) {
        printf("[MQTT] ESP8266 init failed, mqtt_task exit.\r\n");
        vTaskDelete(NULL);
        return;
    }

    mqtt_client_init(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE_SEC);

    mqtt_phase_t phase = MQTT_PHASE_INIT;
    TickType_t phase_tick = xTaskGetTickCount();
    char report_buf[MQTT_REPORT_BUF_SIZE];

    printf("[MQTT] enter main loop\r\n");

    for (;;)
    {
        mqtt_loop();

        switch (phase)
        {
            case MQTT_PHASE_INIT: {
                if (esp8266_connect_wifi(MQTT_WIFI_SSID, MQTT_WIFI_PASS) == ESP_OK) {
                    printf("[MQTT] WiFi connected, RSSI=%d dBm\r\n", (int)esp8266_get_rssi());
                    phase = MQTT_PHASE_MQTT_INIT;
                    phase_tick = xTaskGetTickCount();
                } else {
                    printf("[MQTT] WiFi connect failed, retry in 5s\r\n");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
                break;
            }

            case MQTT_PHASE_MQTT_INIT: {
                /* TCP 连接交给 mqtt_connect_broker() 内部处理 */
                phase = MQTT_PHASE_CONNECT;
                phase_tick = xTaskGetTickCount();
                break;
            }

            case MQTT_PHASE_CONNECT: {
                if (mqtt_connect_broker(MQTT_BROKER_HOST, MQTT_BROKER_PORT) == MQTT_OK) {
                    printf("[MQTT] CONNECTED to broker\r\n");
                    phase = MQTT_PHASE_SUB;
                    phase_tick = xTaskGetTickCount();
                } else {
                    printf("[MQTT] CONNECT failed, retry\r\n");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                }
                break;
            }

            case MQTT_PHASE_SUB: {
                if (mqtt_subscribe(MQTT_TOPIC_CTRL, MQTT_QOS_0) == MQTT_OK) {
                    printf("[MQTT] subscribed %s\r\n", MQTT_TOPIC_CTRL);
                    phase = MQTT_PHASE_RUN;
                    phase_tick = xTaskGetTickCount();
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;
            }

            case MQTT_PHASE_RUN: {
                if (!mqtt_is_connected()) {
                    printf("[MQTT] lost connection, reconnect\r\n");
                    mqtt_disconnect();
                    phase = MQTT_PHASE_INIT;
                    phase_tick = xTaskGetTickCount();
                    break;
                }

                if (ReportQueue != NULL) {
                    env_avg_t avg;
                    if (xQueueReceive(ReportQueue, &avg, 0) == pdPASS) {
                        int n = snprintf(report_buf, sizeof(report_buf),
                            "{\"t\":%d.%d,\"h\":%d.%d,\"rain\":%u,\"co2\":%u,\"co\":%u,\"err\":%u}",
                            (int)(avg.temp_avg_x10 / 10), (int)(avg.temp_avg_x10 % 10),
                            (int)(avg.humi_avg_x10 / 10), (int)(avg.humi_avg_x10 % 10),
                            (unsigned)avg.rain_flag,
                            (unsigned)avg.co2_avg,
                            (unsigned)avg.co_avg,
                            (unsigned)avg.err_bits);
                        if (n > 0) {
                            mqtt_err_t ret = mqtt_publish(
                                MQTT_TOPIC_REPORT,
                                (const uint8_t *)report_buf,
                                (uint16_t)n,
                                MQTT_QOS_0);
                            if (ret == MQTT_OK) {
                                printf("[MQTT-TX] OK %s\r\n", report_buf);
                            } else {
                                printf("[MQTT-TX] FAIL ret=%d %s\r\n",
                                       (int)ret, report_buf);
                            }
                        }
                    }
                }

                vTaskDelay(100);
                break;
            }

            default:
                phase = MQTT_PHASE_INIT;
                break;
        }

        /* 阶段卡死超时保护：30s 还在同一阶段 → 重置 */
        if (phase != MQTT_PHASE_RUN) {
            if ((xTaskGetTickCount() - phase_tick) > pdMS_TO_TICKS(30000)) {
                printf("[MQTT] phase stuck, reset\r\n");
                mqtt_disconnect();
                phase = MQTT_PHASE_INIT;
                phase_tick = xTaskGetTickCount();
            }
        }
    }
}
