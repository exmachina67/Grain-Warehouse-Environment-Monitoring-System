/**
 ****************************************************************************************************
 * @file        esp8266.h
 * @brief       ESP8266 WiFi 驱动（AT 指令版）— 与 MQTT 解耦
 *
 * 说明：
 *   本模块只管 ESP8266 的 AT 通信、WiFi 连接、TCP 连接与收发。
 *
 * 硬件接线：
 *   ESP8266 TX  -> STM32 PA1 (UART4_RX)
 *   ESP8266 RX  -> STM32 PA0 (UART4_TX)
 *   ESP8266 VCC -> 3.3V
 *   ESP8266 GND -> GND
 *   ESP8266 EN  -> STM32 PC4 (可选)
 *   ESP8266 RST -> STM32 PC5 (可选)
 *
 * 接收模型：
 *   - 自带环形缓冲区（不依赖 usart.c 的 g_usart2_rx_buf）
 *   - 接收中断把字节直接入环形缓冲
 *   - esp8266_loop() 解析 +IPD 帧并回调上层
 *   - AT 行响应解析也走这个环形缓冲
 ****************************************************************************************************
 */

#ifndef __ESP8266_H
#define __ESP8266_H

#include "./SYSTEM/SYS/sys.h"
#include <stdint.h>

/* ESP8266 控制脚（可选；不接 EN/RST 也能跑） */
#define ESP_EN_GPIO_PORT        GPIOC
#define ESP_EN_GPIO_PIN         GPIO_PIN_4
#define ESP_RST_GPIO_PORT       GPIOC
#define ESP_RST_GPIO_PIN        GPIO_PIN_5

/* AT 串口：UART4 (PA0/PA1) — 自管环形缓冲，不依赖 usart.c */
#define ESP_UART                UART4
#define ESP_UART_IRQn           UART4_IRQn
#define ESP_UART_IRQHandler     UART4_IRQHandler
#define ESP_UART_CLK_ENABLE()   do{ __HAL_RCC_UART4_CLK_ENABLE(); }while(0)

#define ESP_TX_GPIO_PORT        GPIOA
#define ESP_TX_GPIO_PIN         GPIO_PIN_0
#define ESP_TX_GPIO_AF          GPIO_AF8_UART4
#define ESP_TX_GPIO_CLK_ENABLE()  do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

#define ESP_RX_GPIO_PORT        GPIOA
#define ESP_RX_GPIO_PIN         GPIO_PIN_1
#define ESP_RX_GPIO_AF          GPIO_AF8_UART4
#define ESP_RX_GPIO_CLK_ENABLE()  do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

/* 环形缓冲容量（必须为 2 的幂） */
#define ESP_RXBUF_SIZE         1024
#define ESP_RXBUF_MASK         (ESP_RXBUF_SIZE - 1)

/* 多连接 link_id 范围 */
#define ESP_MAX_LINK_ID         5

/* 返回码 */
typedef enum {
    ESP_OK = 0,
    ESP_ERR_TIMEOUT,
    ESP_ERR_NO_RESP,
    ESP_ERR_RESP,
    ESP_ERR_PARAM,
    ESP_ERR_BUSY,
} esp_err_t;

/* 接收回调：link_id（0~4 或 -1 表示非 IP 数据），data/len 即收到的内容 */
typedef void (*esp_recv_cb_t)(int8_t link_id, const uint8_t *data, uint16_t len);

/* ===== 生命周期 ===== */
esp_err_t esp8266_init(void);                         /* 复位 / 关回显 / 设 Station / 多连接 */
void      esp8266_set_recv_cb(esp_recv_cb_t cb);      /* 注册接收回调 */
esp_err_t esp8266_loop(void);                         /* 周期性调用，解析 +IPD 帧并回调 */

/* ===== WiFi ===== */
/* 1= 使用 ESP8266 flash 里已存的 AP（之前用 AT+CWJAP 已经烧进 flash 的），
 *   此时 ssid/pwd 参数被忽略，由 ESP8266 上电自动连接。
 * 0= 调用 AT+CWJAP 用入参 ssid/pwd 连接
 */
#define ESP8266_USE_STORED_AP 0

esp_err_t esp8266_connect_wifi(const char *ssid, const char *pwd);
esp_err_t esp8266_disconnect_wifi(void);
uint8_t   esp8266_wifi_is_connected(void);
int8_t    esp8266_get_rssi(void);                     /* 信号强度 dBm，未连则返回 -127 */

/* ===== TCP 连接 ===== */
/**
 * @brief  创建一个 TCP 连接，返回 link_id
 * @param  host   服务器域名/IP
 * @param  port   端口
 * @retval >=0 link_id；<0 失败
 */
int8_t esp8266_tcp_connect(const char *host, uint16_t port);
esp_err_t esp8266_tcp_close(int8_t link_id);
esp_err_t esp8266_tcp_send(int8_t link_id, const uint8_t *data, uint16_t len);
esp_err_t esp8266_query_tcp_status(int8_t link_id);
esp_err_t esp8266_ping(const char *host);
void       esp8266_print_netinfo(void);                  /* 打印 IP/网关/DNS */
uint8_t esp8266_is_link_up(int8_t link_id);

#endif 

