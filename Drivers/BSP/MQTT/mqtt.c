/**
 ****************************************************************************************************
 * @file        mqtt.c
 * @brief       MQTT 3.1.1 客户端协议层（通过 ESP8266 通信）
 *
 * 简化说明：
 *   - 仅实现 QoS 0 与 QoS 1（QoS 2 流程复杂，物联网场景罕见）
 *   - 仅支持清理会话（clean session = 1）
 *   - 不持久化会话，断线重连后订阅需重新发起
 *   - 仅做最小报文打包/解析，适合上行"传感器数据" + 下行"控制命令"
 *
 * MQTT 报文固定头部（2 字节）：
 *    byte 0:  control packet type | flags<<4
 *    byte 1~: remaining length (变长 1~4 字节)
 ****************************************************************************************************
 */

#include "./BSP/MQTT/mqtt.h"
#include "./BSP/ESP8266/esp8266.h"
#include "./SYSTEM/delay/delay.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdlib.h>

/* ===== MQTT 控制报文类型 ===== */
#define MQTT_CTRL_CONNECT     1
#define MQTT_CTRL_CONNACK     2
#define MQTT_CTRL_PUBLISH     3
#define MQTT_CTRL_PUBACK      4
#define MQTT_CTRL_SUBSCRIBE   8
#define MQTT_CTRL_SUBACK      9
#define MQTT_CTRL_UNSUBSCRIBE 10
#define MQTT_CTRL_UNSUBACK    11
#define MQTT_CTRL_PINGREQ     12
#define MQTT_CTRL_PINGRESP    13
#define MQTT_CTRL_DISCONNECT  14

/* ===== 状态 ===== */
typedef enum {
    MQTT_ST_IDLE = 0,
    MQTT_ST_TCP_OPEN,
    MQTT_ST_CONNECT_SENT,
    MQTT_ST_CONNECTED,
    MQTT_ST_DISCONNECTED,
} mqtt_state_t;

static mqtt_state_t   s_state = MQTT_ST_IDLE;
static uint8_t        s_link_id = 0;          /* ESP8266 上用的 link id */
static uint8_t        s_keepalive = 10;
static uint32_t       s_last_ping_ms = 0;
static uint16_t       s_next_packet_id = 1;
static mqtt_message_cb_t s_msg_cb = NULL;

/* 客户端配置（CONNECT 用） */
static char s_client_id[64];
static char s_user[64];
static char s_pass[64];

/* 接收缓冲（每次 esp8266 回调给我们的报文暂存） */
#define MQTT_RX_BUF_SIZE  512
static uint8_t  s_rx_buf[MQTT_RX_BUF_SIZE];
static uint16_t s_rx_len = 0;

/* 解析暂存（解析 PUBLISH 时 topic 复制到这里） */
static char s_cur_topic[128];

/* ===== 内部工具 ===== */

static uint32_t millis(void)
{
    /* 单调递增的毫秒计数，HAL 默认实现 */
    return HAL_GetTick();
}

/**
 * @brief 变长编码长度（MQTT 协议用）
 * @return 写入字节数
 */
static int mqtt_encode_len(uint8_t *out, uint32_t len)
{
    int n = 0;
    do {
        uint8_t b = len & 0x7F;
        len >>= 7;
        if (len > 0) b |= 0x80;
        out[n++] = b;
    } while (len > 0);
    return n;
}

/**
 * @brief 变长解码
 */
static int mqtt_decode_len(const uint8_t *in, uint16_t in_len, uint32_t *out, uint8_t *out_bytes)
{
    uint32_t multiplier = 1, value = 0;
    uint8_t i = 0;
    do {
        if (i >= in_len) return -1;
        value += (uint32_t)(in[i] & 0x7F) * multiplier;
        if (multiplier > 128*128*128) return -1;
        multiplier *= 128;
        i++;
    } while ((in[i-1] & 0x80) != 0);
    *out = value;
    *out_bytes = i;
    return 0;
}

/**
 * @brief 16-bit 大端写
 */
static void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/**
 * @brief 16-bit 大端读
 */
static uint16_t read_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

/* ===== 报文构造 ===== */

static int build_connect(uint8_t *out, uint16_t out_cap)
{
    /* 可变头：Protocol Name "MQTT" + Level 4 + Flags + Keepalive */
    uint8_t var[16];
    int vlen = 0;
    /* Protocol Name */
    var[vlen++] = 0x00;
    var[vlen++] = 0x04;
    var[vlen++] = 'M';
    var[vlen++] = 'Q';
    var[vlen++] = 'T';
    var[vlen++] = 'T';
    
    /* Protocol Level = 4 */
    var[vlen++] = 0x04;
    
    /* Connect Flags: clean session + user/password 标记 */
    uint8_t flags = 0x02;   /* clean session */
    if (s_user[0]) flags |= 0x80;
    if (s_pass[0]) flags |= 0x40;
    var[vlen++] = flags;
    
    /* Keepalive */
    var[vlen++] = (uint8_t)(s_keepalive >> 8);
    var[vlen++] = (uint8_t)s_keepalive;

    /* Payload：Client ID + [Will] + [User] + [Pass] */
    uint8_t payload[256];
    int plen = 0;
    if (s_client_id[0] == 0) {
        payload[plen++] = 0x00;
        payload[plen++] = 0x00;
    } else {
        uint16_t n = (uint16_t)strlen(s_client_id);
        write_u16(payload + plen, n); plen += 2;
        memcpy(payload + plen, s_client_id, n); plen += n;
    }
    if (s_user[0]) {
        uint16_t n = (uint16_t)strlen(s_user);
        write_u16(payload + plen, n); plen += 2;
        memcpy(payload + plen, s_user, n); plen += n;
    }
    if (s_pass[0]) {
        uint16_t n = (uint16_t)strlen(s_pass);
        write_u16(payload + plen, n); plen += 2;
        memcpy(payload + plen, s_pass, n); plen += n;
    }

    /* 固定头 + 剩余长度 */
    uint32_t rem = (uint32_t)vlen + (uint32_t)plen;
    if (out_cap < 2 + 4 + rem) return -1;
    out[0] = (uint8_t)(MQTT_CTRL_CONNECT << 4);
    int n = mqtt_encode_len(out + 1, rem);
    memcpy(out + 1 + n, var, vlen);
    memcpy(out + 1 + n + vlen, payload, plen);
    return 1 + n + (int)rem;
}

static int build_publish(uint8_t *out, uint16_t out_cap,
                         const char *topic, const uint8_t *payload, uint16_t len,
                         uint8_t qos, uint16_t packet_id)
{
    uint16_t tn = (uint16_t)strlen(topic);
    uint32_t rem = 2 + tn + ((qos > 0) ? 2 : 0) + len;
    if (out_cap < 2 + 4 + rem) return -1;

    uint8_t flags = 0;
    if (qos > 0) flags |= (qos << 1);
    out[0] = (uint8_t)((MQTT_CTRL_PUBLISH << 4) | flags);
    int n = mqtt_encode_len(out + 1, rem);
    uint8_t *p = out + 1 + n;
    write_u16(p, tn); p += 2;
    memcpy(p, topic, tn); p += tn;
    if (qos > 0) {
        write_u16(p, packet_id); p += 2;
    }
    memcpy(p, payload, len); p += len;
    return (int)(p - out);
}

static int build_subscribe(uint8_t *out, uint16_t out_cap,
                           const char *topic, uint8_t qos, uint16_t packet_id)
{
    uint16_t tn = (uint16_t)strlen(topic);
    /* 剩余长度 = 2(packet_id) + 2(topic_len) + tn + 1(qos) */
    uint32_t rem = 2 + 2 + tn + 1;
    if (out_cap < 2 + 4 + rem) return -1;

    out[0] = (uint8_t)((MQTT_CTRL_SUBSCRIBE << 4) | 0x02);   /* bit1 必须置 1 */
    int n = mqtt_encode_len(out + 1, rem);
    uint8_t *p = out + 1 + n;
    write_u16(p, packet_id); p += 2;
    write_u16(p, tn); p += 2;
    memcpy(p, topic, tn); p += tn;
    *p++ = qos;
    return (int)(p - out);
}

static int build_unsubscribe(uint8_t *out, uint16_t out_cap,
                             const char *topic, uint16_t packet_id)
{
    uint16_t tn = (uint16_t)strlen(topic);
    uint32_t rem = 2 + 2 + tn;
    if (out_cap < 2 + 4 + rem) return -1;

    out[0] = (uint8_t)((MQTT_CTRL_UNSUBSCRIBE << 4) | 0x02);
    int n = mqtt_encode_len(out + 1, rem);
    uint8_t *p = out + 1 + n;
    write_u16(p, packet_id); p += 2;
    write_u16(p, tn); p += 2;
    memcpy(p, topic, tn); p += tn;
    return (int)(p - out);
}

static int build_pingreq(uint8_t *out, uint16_t out_cap)
{
    if (out_cap < 2) return -1;
    out[0] = (uint8_t)(MQTT_CTRL_PINGREQ << 4);
    out[1] = 0;
    return 2;
}

static int build_disconnect(uint8_t *out, uint16_t out_cap)
{
    if (out_cap < 2) return -1;
    out[0] = (uint8_t)(MQTT_CTRL_DISCONNECT << 4);
    out[1] = 0;
    return 2;
}

/* ===== 报文解析 ===== */

/* 解析 broker 下行的 PUBLISH */
static void handle_publish(const uint8_t *buf, uint16_t len)
{
    if (len < 4) return;
    uint8_t flags = buf[0] & 0x0F;
    uint8_t qos = (flags >> 1) & 0x03;
    uint32_t rem;
    uint8_t  rem_bytes;
    if (mqtt_decode_len(buf + 1, len - 1, &rem, &rem_bytes) != 0) return;
    const uint8_t *p = buf + 1 + rem_bytes;
    const uint8_t *end = buf + 1 + rem_bytes + rem;
    if (end > buf + len) end = buf + len;
    if (p + 2 > end) return;
    uint16_t tn = read_u16(p); p += 2;
    if (p + tn > end) return;
    if (tn >= sizeof(s_cur_topic)) tn = sizeof(s_cur_topic) - 1;
    memcpy(s_cur_topic, p, tn); s_cur_topic[tn] = 0;
    p += tn;
    uint16_t pid = 0;
    if (qos > 0) {
        if (p + 2 > end) return;
        pid = read_u16(p); p += 2;
    }
    uint16_t plen = (uint16_t)(end - p);

    if (s_msg_cb) {
        mqtt_message_t m = {
            .topic = s_cur_topic,
            .topic_len = tn,
            .payload = p,
            .payload_len = plen,
        };
        s_msg_cb(&m);
    }

    /* QoS 1 要回 PUBACK */
    if (qos == 1) {
        uint8_t ack[4];
        ack[0] = (uint8_t)(MQTT_CTRL_PUBACK << 4);
        ack[1] = 2;
        write_u16(ack + 2, pid);
        esp8266_tcp_send(s_link_id, ack, 4);
    }
    /* QoS 2 此略 */
}

/* broker 上行响应分发 */
static void handle_rx(const uint8_t *data, uint16_t len)
{
    while (len >= 2) {
        uint8_t type = (data[0] >> 4) & 0x0F;
        uint32_t rem;
        uint8_t  rem_bytes;
        if (mqtt_decode_len(data + 1, len - 1, &rem, &rem_bytes) != 0) break;
        if (1 + rem_bytes + rem > len) break;  /* 不完整 */

        const uint8_t *payload = data + 1 + rem_bytes;
        uint16_t plen = (uint16_t)rem;   /* 注：CONNACK 含 2 字节固定头 + 1 字节原因码 */

        switch (type) {
            case MQTT_CTRL_CONNACK:
                /* payload[0] = Acknowledge Flags, payload[1] = Connect Return Code */
                if (plen >= 2 && payload[1] == 0) {
                    s_state = MQTT_ST_CONNECTED;
                    s_last_ping_ms = millis();   /* 重置心跳起点 */
                    printf("[MQTT] CONNACK OK\r\n");
                } else {
                    printf("[MQTT] CONNACK reject: plen=%u flags=0x%02X rc=0x%02X\r\n",
                           (unsigned)plen, payload[0],
                           (plen >= 2) ? payload[1] : 0xFF);
                    s_state = MQTT_ST_DISCONNECTED;
                }
                break;
            case MQTT_CTRL_PUBACK:
                /* 仅 QoS 1，丢弃 */
                break;
            case MQTT_CTRL_SUBACK:
                /* 可校验返回码（0x80=失败） */
                break;
            case MQTT_CTRL_PINGRESP:
                s_last_ping_ms = millis();
                break;
            case MQTT_CTRL_PUBLISH:
                /* 处理完整 PUBLISH 报文 */
                handle_publish(data, (uint16_t)(1 + rem_bytes + rem));
                break;
            default:
                break;
        }

        data += 1 + rem_bytes + rem;
        len  -= (uint16_t)(1 + rem_bytes + rem);
    }
}

/* ===== ESP8266 接收回调 ===== */
static void on_esp_recv(int8_t link_id, const uint8_t *data, uint16_t len)
{
    if (link_id != s_link_id) return;
    if (len > MQTT_RX_BUF_SIZE) len = MQTT_RX_BUF_SIZE;
    memcpy(s_rx_buf, data, len);
    s_rx_len = len;
    handle_rx(s_rx_buf, s_rx_len);
}

/* ===== 公开 API ===== */

mqtt_err_t mqtt_client_init(const char *client_id, const char *user,
                            const char *pass, uint16_t keepalive_sec)
{
    if (!client_id) return MQTT_ERR_PARAM;
    strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);
    s_client_id[sizeof(s_client_id) - 1] = 0;
    if (user) {
        strncpy(s_user, user, sizeof(s_user) - 1);
        s_user[sizeof(s_user) - 1] = 0;
    } else s_user[0] = 0;
    if (pass) {
        strncpy(s_pass, pass, sizeof(s_pass) - 1);
        s_pass[sizeof(s_pass) - 1] = 0;
    } else s_pass[0] = 0;
    s_keepalive = (uint8_t)(keepalive_sec ? keepalive_sec : 10);
    s_state = MQTT_ST_IDLE;
    s_last_ping_ms = millis();

    esp8266_set_recv_cb(on_esp_recv);
    return MQTT_OK;
}

mqtt_err_t mqtt_connect_broker(const char *host, uint16_t port)
{
    if (!host) return MQTT_ERR_PARAM;
    if (!esp8266_wifi_is_connected()) return MQTT_ERR_NET;

    int8_t link = esp8266_tcp_connect(host, port);
    if (link < 0) return MQTT_ERR_NET;
    s_link_id = (uint8_t)link;
    s_state = MQTT_ST_TCP_OPEN;

    /* 发送 CONNECT */
    uint8_t buf[256];
    int n = build_connect(buf, sizeof(buf));
    if (n < 0) return MQTT_ERR_PARAM;
    s_state = MQTT_ST_CONNECT_SENT;
    if (esp8266_tcp_send(s_link_id, buf, (uint16_t)n) != ESP_OK) {
        s_state = MQTT_ST_DISCONNECTED;
        return MQTT_ERR_NET;
    }

    /* 等待 CONNACK，10 秒与 task_mqtt 对齐 */
    uint32_t start = millis();
    while (millis() - start < 10000) {
        if (s_state == MQTT_ST_CONNECTED) return MQTT_OK;
        if (s_state == MQTT_ST_DISCONNECTED) {
            esp8266_tcp_close(s_link_id);
            return MQTT_ERR_RESP;
        }
        mqtt_loop();
    }
    /* 超时：关闭旧 TCP，避免下一次重连时旧连接残留 */
    esp8266_tcp_close(s_link_id);
    s_state = MQTT_ST_DISCONNECTED;
    return MQTT_ERR_TIMEOUT;
}

mqtt_err_t mqtt_disconnect(void)
{
    if (s_state != MQTT_ST_CONNECTED && s_state != MQTT_ST_TCP_OPEN &&
        s_state != MQTT_ST_CONNECT_SENT) return MQTT_ERR_STATE;

    uint8_t buf[2];
    int n = build_disconnect(buf, sizeof(buf));
    if (n > 0) (void)esp8266_tcp_send(s_link_id, buf, (uint16_t)n);
    esp8266_tcp_close(s_link_id);
    s_state = MQTT_ST_IDLE;
    return MQTT_OK;
}

mqtt_err_t mqtt_publish(const char *topic, const uint8_t *payload, uint16_t len, uint8_t qos)
{
    if (s_state != MQTT_ST_CONNECTED) return MQTT_ERR_STATE;
    if (!topic || !payload) return MQTT_ERR_PARAM;
    if (qos > 1) qos = 1;   /* 此实现限 QoS 0/1 */

    uint16_t pid = 0;
    if (qos > 0) {
        pid = s_next_packet_id++;
        if (s_next_packet_id == 0) s_next_packet_id = 1;
    }

    uint8_t buf[512];
    int n = build_publish(buf, sizeof(buf), topic, payload, len, qos, pid);
    if (n < 0) return MQTT_ERR_PARAM;
    if (esp8266_tcp_send(s_link_id, buf, (uint16_t)n) != ESP_OK) return MQTT_ERR_NET;
    return MQTT_OK;
}

mqtt_err_t mqtt_subscribe(const char *topic, uint8_t qos)
{
    if (s_state != MQTT_ST_CONNECTED) return MQTT_ERR_STATE;
    if (!topic) return MQTT_ERR_PARAM;

    uint16_t pid = s_next_packet_id++;
    if (s_next_packet_id == 0) s_next_packet_id = 1;

    uint8_t buf[256];
    int n = build_subscribe(buf, sizeof(buf), topic, qos, pid);
    if (n < 0) return MQTT_ERR_PARAM;
    if (esp8266_tcp_send(s_link_id, buf, (uint16_t)n) != ESP_OK) return MQTT_ERR_NET;
    return MQTT_OK;
}

mqtt_err_t mqtt_unsubscribe(const char *topic)
{
    if (s_state != MQTT_ST_CONNECTED) return MQTT_ERR_STATE;
    if (!topic) return MQTT_ERR_PARAM;

    uint16_t pid = s_next_packet_id++;
    if (s_next_packet_id == 0) s_next_packet_id = 1;

    uint8_t buf[256];
    int n = build_unsubscribe(buf, sizeof(buf), topic, pid);
    if (n < 0) return MQTT_ERR_PARAM;
    if (esp8266_tcp_send(s_link_id, buf, (uint16_t)n) != ESP_OK) return MQTT_ERR_NET;
    return MQTT_OK;
}

void mqtt_set_message_cb(mqtt_message_cb_t cb)
{
    s_msg_cb = cb;
}

void mqtt_loop(void)
{
    esp8266_loop();

    if (s_state == MQTT_ST_CONNECTED) {
        uint32_t now = millis();
        /* 心跳：每 keepalive 秒发送 PINGREQ（broker 端 1.5 倍超时） */
        if (now - s_last_ping_ms >= (uint32_t)s_keepalive * 1000U) {
            uint8_t buf[2];
            int n = build_pingreq(buf, sizeof(buf));
            if (n > 0) {
                if (esp8266_tcp_send(s_link_id, buf, (uint16_t)n) == ESP_OK) {
                    printf("[MQTT] PINGREQ sent\r\n");
                }
            }
            s_last_ping_ms = now;
        }
    }
}

uint8_t mqtt_is_connected(void)
{
    return s_state == MQTT_ST_CONNECTED;
}



