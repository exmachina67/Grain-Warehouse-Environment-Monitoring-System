/**
 ****************************************************************************************************
 * @file        mqtt.h
 * @brief       MQTT 3.1.1 协议层（通过 ESP8266 驱动通信）
 ****************************************************************************************************
 */

#ifndef __MQTT_H
#define __MQTT_H

#include <stdint.h>

/* QoS 等级 */
#define MQTT_QOS_0    0
#define MQTT_QOS_1    1
#define MQTT_QOS_2    2

/* 返回码 */
typedef enum {
    MQTT_OK = 0,
    MQTT_ERR_TIMEOUT,
    MQTT_ERR_RESP,
    MQTT_ERR_PARAM,
    MQTT_ERR_STATE,
    MQTT_ERR_NET,
} mqtt_err_t;

/* 收到下行消息（已根据 topic 解析） */
typedef struct {
    const char *topic;        /* 主题（以 \0 结尾） */
    uint16_t    topic_len;
    const uint8_t *payload;   /* 消息体 */
    uint16_t    payload_len;
} mqtt_message_t;

/* 收到下行消息时的回调 */
typedef void (*mqtt_message_cb_t)(const mqtt_message_t *msg);

/**
 * @brief 初始化 MQTT 客户端（不联网，只是分配状态）
 */
mqtt_err_t mqtt_client_init(const char *client_id,
                            const char *user,
                            const char *pass,
                            uint16_t keepalive_sec);

/**
 * @brief 连接到 MQTT Broker（要求 ESP8266 已连 WiFi）
 */
mqtt_err_t mqtt_connect_broker(const char *host, uint16_t port);

/**
 * @brief 断开并清理
 */
mqtt_err_t mqtt_disconnect(void);

/**
 * @brief 发布消息
 */
mqtt_err_t mqtt_publish(const char *topic,
                        const uint8_t *payload, uint16_t len,
                        uint8_t qos);

/**
 * @brief 订阅 topic
 */
mqtt_err_t mqtt_subscribe(const char *topic, uint8_t qos);

/**
 * @brief 取消订阅
 */
mqtt_err_t mqtt_unsubscribe(const char *topic);

/**
 * @brief 注册下行消息回调
 */
void mqtt_set_message_cb(mqtt_message_cb_t cb);

/**
 * @brief 周期性处理（心跳、下行分发），建议 50ms~500ms 调用一次
 */
void mqtt_loop(void);

/**
 * @brief 状态查询
 */
uint8_t mqtt_is_connected(void);

#endif

