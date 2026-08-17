/**
 ****************************************************************************************************
 * @file        esp8266.c
 * @brief       ESP8266 驱动 — 自管 UART4 (PA0/PA1) + 自管环形缓冲
 *
 *  - 接收：UART4 中断 → 字节入环形缓冲 → esp8266_loop() 解析 +IPD 帧或 AT 行
 *  - 发送：阻塞式 HAL_UART_Transmit
 *  - 不依赖工程 usart.c 的任何缓冲
 *  - 字节级环形缓冲，不依赖 \r\n，可承载 MQTT 二进制
 ****************************************************************************************************
 */

#include "./BSP/ESP8266/esp8266.h"
#include "./SYSTEM/delay/delay.h"
#include <string.h>
#include <stdio.h>

/* ===== 环形缓冲 ===== */
typedef struct {
    uint8_t  buf[ESP_RXBUF_SIZE];
    uint16_t head;   /* 写入位置 */
    uint16_t tail;   /* 读取位置 */
} ring_t;

static ring_t s_rx = { {0}, 0, 0 };

static inline uint16_t ring_used(const ring_t *r)
{
    return (uint16_t)((r->head - r->tail) & ESP_RXBUF_MASK);
}

static inline uint16_t ring_free(const ring_t *r)
{
    return (uint16_t)(ESP_RXBUF_SIZE - 1 - ring_used(r));
}

static inline int ring_push(ring_t *r, uint8_t b)
{
    if (ring_free(r) == 0) return -1;
    r->buf[r->head] = b;
    r->head = (r->head + 1) & ESP_RXBUF_MASK;
    return 0;
}

/* 消费环形缓冲前 n 字节 */
static void ring_consume(ring_t *r, uint16_t n)
{
    uint16_t used = ring_used(r);
    if (n > used) n = used;
    r->tail = (r->tail + n) & ESP_RXBUF_MASK;
}

/* 把环形缓冲里 [tail, head) 区间拷贝出连续内存（保证不破坏环结构） */
static uint16_t ring_copy_out(ring_t *r, uint8_t *dst, uint16_t maxlen)
{
    uint16_t n = 0;
    uint16_t used = ring_used(r);
    if (maxlen > used) maxlen = used;
    while (n < maxlen) {
        dst[n++] = r->buf[r->tail];
        r->tail = (r->tail + 1) & ESP_RXBUF_MASK;
    }
    return n;
}

/* ===== UART4 句柄 ===== */
static UART_HandleTypeDef s_huart;

/* ===== 状态 ===== */
static uint8_t        s_wifi_connected = 0;
static uint8_t        s_link_state[ESP_MAX_LINK_ID] = {0};
static esp_recv_cb_t  s_recv_cb = NULL;

/* 前向声明：init() 调用 print_netinfo() */
void esp8266_print_netinfo(void);

/* ===== UART4 接收中断（HAL 库单字节 IT 模式） ===== */
void ESP_UART_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&s_huart, UART_FLAG_RXNE) != RESET) {
        uint8_t b = (uint8_t)(s_huart.Instance->DR & 0xFF);
        (void)ring_push(&s_rx, b);
    }
    /* HAL 库可省略 RxCpltCallback（用 IT 单字节） */
}

/* ===== 内部辅助 ===== */

static void uart_send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&s_huart, (uint8_t *)data, len, 1000);
}

static void at_send_line(const char *line)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s\r\n", line);
    if (n > 0) uart_send((uint8_t *)buf, (uint16_t)n);
}

/**
 * @brief 在环形缓冲里找 needle；返回 0 找到；并把 needle 之前（不含 needle）的字节丢弃
 * @param needle       期望字符串
 * @param timeout_ms   超时
 * @param out_line     若非 NULL，把 needle 所在行的全部内容（去掉 \r）拷贝出来
 */
static int at_wait_for(const char *needle, uint32_t timeout_ms, char *out_line)
{
    uint32_t elapsed = 0;
    uint16_t need = (uint16_t)strlen(needle);

    while (elapsed < timeout_ms) {
        uint16_t used = ring_used(&s_rx);
        if (used >= need) {
            /* 找到 needle */
            uint16_t i;
            int found = -1;
            for (i = 0; i + need <= used; i++) {
                int match = 1;
                for (uint16_t k = 0; k < need; k++) {
                    uint16_t idx = (s_rx.tail + i + k) & ESP_RXBUF_MASK;
                    if (s_rx.buf[idx] != (uint8_t)needle[k]) { match = 0; break; }
                }
                if (match) { found = (int)i; break; }
            }
            if (found >= 0) {
                /* 找到完整行：把 needle 所在行的全部内容（去掉 \r）拷贝到 out_line */
                if (out_line) {
                    int n = 0;
                    /* needle 所在位置之后的字符开始抄，到 \r 停 */
                    for (uint16_t k = (uint16_t)found; k < used && n < 200; k++) {
                        uint16_t idx = (s_rx.tail + k) & ESP_RXBUF_MASK;
                        char c = (char)s_rx.buf[idx];
                        if (c == '\r') break;
                        out_line[n++] = c;
                    }
                    out_line[n] = 0;
                }
                /* 消费到 needle 所在行的 \n 之后（不消费前面的行） */
                uint16_t consume = 0;
                for (uint16_t k = (uint16_t)found; k < used; k++) {
                    uint16_t idx = (s_rx.tail + k) & ESP_RXBUF_MASK;
                    consume++;
                    if (s_rx.buf[idx] == '\n') break;
                }
                ring_consume(&s_rx, consume);
                return 0;
            }
        }
        delay_ms(10);
        elapsed += 10;
    }
    return -1;
}

/* 在 +IPD 帧里解析 link_id 和数据长度 */
static void parse_ipd_header(const char *line, int8_t *out_link, uint16_t *out_len)
{
    *out_link = -1;
    *out_len = 0;
    /* 兼容 CIPMUX=0 ("+IPD,n:") 和 CIPMUX=1 ("+IPD,id,n:") */
    if (strncmp(line, "+IPD,", 5) != 0) return;
    const char *p = line + 5;
    int link = 0;
    int got_link = 0;

    /* 试读一段数字 */
    const char *q = p;
    while (*q >= '0' && *q <= '9') q++;
    /* q 指向第一个非数字
     *   - 如果后面是 ',' → 这个数字是 link_id，后面再读 len
     *   - 如果后面是 ':' → 这个数字是 len（CIPMUX=0 模式），link 默认为 0
     */
    if (*q == ',') {
        link = 0;
        while (*p >= '0' && *p <= '9') { link = link * 10 + (*p - '0'); p++; }
        if (*p != ',') return;
        p++;
        got_link = 1;
    }
    int len = 0;
    while (*p >= '0' && *p <= '9') { len = len * 10 + (*p - '0'); p++; }
    if (*p != ':') return;
    (void)got_link;
    *out_link = (int8_t)link;
    *out_len = (uint16_t)len;
}

/* ===== ESP8266 硬件初始化 ===== */
static esp_err_t uart4_init(uint32_t baud)
{
    GPIO_InitTypeDef gpio = {0};

    ESP_UART_CLK_ENABLE();
    ESP_TX_GPIO_CLK_ENABLE();
    ESP_RX_GPIO_CLK_ENABLE();

    gpio.Pin       = ESP_TX_GPIO_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = ESP_TX_GPIO_AF;
    HAL_GPIO_Init(ESP_TX_GPIO_PORT, &gpio);

    gpio.Pin       = ESP_RX_GPIO_PIN;
    gpio.Alternate = ESP_RX_GPIO_AF;
    HAL_GPIO_Init(ESP_RX_GPIO_PORT, &gpio);

    s_huart.Instance          = ESP_UART;
    s_huart.Init.BaudRate     = baud;
    s_huart.Init.WordLength   = UART_WORDLENGTH_8B;
    s_huart.Init.StopBits     = UART_STOPBITS_1;
    s_huart.Init.Parity       = UART_PARITY_NONE;
    s_huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_huart.Init.Mode         = UART_MODE_TX_RX;
    s_huart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&s_huart) != HAL_OK) return ESP_ERR_RESP;

    /* 开启接收中断（RXNE 方式） */
    __HAL_UART_ENABLE_IT(&s_huart, UART_IT_RXNE);
    HAL_NVIC_SetPriority(ESP_UART_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(ESP_UART_IRQn);

    return ESP_OK;
}

static void esp_hw_reset(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin   = ESP_EN_GPIO_PIN | ESP_RST_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    HAL_GPIO_WritePin(ESP_RST_GPIO_PORT, ESP_RST_GPIO_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ESP_EN_GPIO_PORT,  ESP_EN_GPIO_PIN,  GPIO_PIN_RESET);
    delay_ms(100);
    HAL_GPIO_WritePin(ESP_EN_GPIO_PORT,  ESP_EN_GPIO_PIN,  GPIO_PIN_SET);
    delay_ms(100);
    HAL_GPIO_WritePin(ESP_RST_GPIO_PORT, ESP_RST_GPIO_PIN, GPIO_PIN_SET);
    delay_ms(500);
}

/* ===== 解析 +IPD 帧（每次 esp8266_loop 调用） ===== */
static void parse_ipd_frames(void)
{
    /* 循环解析直到环形缓冲里没有完整 +IPD 帧 */
    while (1) {
        uint16_t used = ring_used(&s_rx);
        if (used < 6) return;  /* 不足 "+IPD,0,1:" */

        /* 找 +IPD 起始 */
        uint16_t hit = 0xFFFF;
        for (uint16_t i = 0; i + 5 <= used; i++) {
            int match = 1;
            const char *needle = "+IPD,";
            for (uint16_t k = 0; k < 5; k++) {
                uint16_t idx = (s_rx.tail + i + k) & ESP_RXBUF_MASK;
                if (s_rx.buf[idx] != (uint8_t)needle[k]) { match = 0; break; }
            }
            if (match) { hit = i; break; }
        }
        if (hit == 0xFFFF) {
            /* 没找到 +IPD — 仅在处理完所有非 +IPD 数据（如 OK、> 等）后退出 */
            return;
        }

        /* 复制前缀 + 头到临时缓冲用于解析 */
        char hdr[64] = {0};
        uint16_t hdr_len = 0;
        for (uint16_t k = hit; k < used && hdr_len < sizeof(hdr) - 1; k++) {
            uint16_t idx = (s_rx.tail + k) & ESP_RXBUF_MASK;
            char c = (char)s_rx.buf[idx];
            hdr[hdr_len++] = c;
            if (c == ':') break;
        }
        if (hdr[hdr_len - 1] != ':') {
            /* 头还不完整，等下次 */
            return;
        }

        int8_t link;
        uint16_t dlen;
        parse_ipd_header(hdr, &link, &dlen);
        if (link < 0 || dlen == 0) {
            /* 解析失败，丢掉 1 字节继续 */
            ring_consume(&s_rx, 1);
            continue;
        }

        /* 所需总字节 = hit + hdr_len + dlen */
        if (used < hit + hdr_len + dlen) {
            /* 数据未到齐，等下次 */
            return;
        }

        /* 消费 prefix（hit + hdr_len） */
        ring_consume(&s_rx, (uint16_t)(hit + hdr_len));

        /* 取出 dlen 字节作为 data */
        /* 使用 512B 栈缓冲；MQTT 报文通常 ≤ 数百字节 */
        uint8_t ipd_buf[512];
        if (dlen > sizeof(ipd_buf)) {
            /* 超过栈缓冲，跳过该帧 */
            ring_consume(&s_rx, dlen);
            continue;
        }
        uint16_t got = ring_copy_out(&s_rx, ipd_buf, dlen);
        if (got != dlen) {
            /* 异常，丢弃 */
            ring_consume(&s_rx, (uint16_t)(used - hit - hdr_len));
            continue;
        }

        if (s_recv_cb) {
            s_recv_cb(link, ipd_buf, dlen);
        }
    }
}

/* ===== 公开 API ===== */

esp_err_t esp8266_init(void)
{
    char line[201];

    /* step1: UART4 初始化 */
    esp_err_t r = uart4_init(115200);
    if (r != ESP_OK) {
        printf("[ESP] init step1 FAIL: uart4 (r=%d)\r\n", (int)r);
        return r;
    }
    printf("[ESP] init step1 OK: uart4 115200\r\n");

//    /* step2: 硬件复位（暂注释，避免清掉 flash 里的 AP） */
//    esp_hw_reset();

    /* step3: 关回显 ATE0 */
    ring_consume(&s_rx, ring_used(&s_rx));
    at_send_line("ATE0");
    if (at_wait_for("OK", 1500, line) != 0) {
        printf("[ESP] init step3 FAIL: ATE0 (raw=\"%s\")\r\n", line);
        return ESP_ERR_NO_RESP;
    }
    printf("[ESP] init step3 OK: echo off\r\n");

    /* step4: Station 模式 */
    ring_consume(&s_rx, ring_used(&s_rx));
    at_send_line("AT+CWMODE=1");
    if (at_wait_for("OK", 1500, line) != 0) {
        printf("[ESP] init step4 FAIL: CWMODE (raw=\"%s\")\r\n", line);
        return ESP_ERR_NO_RESP;
    }
    printf("[ESP] init step4 OK: station mode\r\n");

    /* step5: AT 握手 */
    int at_ok = 0;
    int at_try = 0;
    for (int i = 0; i < 5; i++) {
        at_try = i;
        ring_consume(&s_rx, ring_used(&s_rx));
        at_send_line("AT");
        if (at_wait_for("OK", 1000, line) == 0) { at_ok = 1; break; }
    }
    if (!at_ok) {
        printf("[ESP] init step5 FAIL: AT no reply\r\n");
        return ESP_ERR_NO_RESP;
    }
    printf("[ESP] init step5 OK: AT reply at try=%d\r\n", at_try);

    /* step6: 单连接（与手动 AT 调试一致） */
    ring_consume(&s_rx, ring_used(&s_rx));
    at_send_line("AT+CIPMUX=0");
    if (at_wait_for("OK", 2000, line) != 0) {
        printf("[ESP] init step6 FAIL: CIPMUX (raw=\"%s\")\r\n", line);
        return ESP_ERR_NO_RESP;
    }
    printf("[ESP] init step6 OK: single conn\r\n");

    /* step7: 关透传 */
    ring_consume(&s_rx, ring_used(&s_rx));
    at_send_line("AT+CIPMODE=0");
    (void)at_wait_for("OK", 1000, line);
    printf("[ESP] init step7 OK: passthrough off\r\n");

    /* step8: 询问当前 AP 链接状态 */
    ring_consume(&s_rx, ring_used(&s_rx));
    at_send_line("AT+CWJAP?");
    if (at_wait_for("+CWJAP:", 3000, line) == 0) {
        printf("[ESP] init step8 OK: AP linked -> %s\r\n", line);
        s_wifi_connected = 1;
    } else {
        printf("[ESP] init step8 WARN: AP not linked yet\r\n");
        s_wifi_connected = 0;
    }

    s_wifi_connected = 0;   /* init 结束时不锁连接状态，留给 connect_wifi 决策 */
    memset(s_link_state, 0, sizeof(s_link_state));
    printf("[ESP] init DONE\r\n");

    /* 初始化后主动查一次 IP，确认 DHCP 拿到 */
    esp8266_print_netinfo();
    return ESP_OK;
}

void esp8266_set_recv_cb(esp_recv_cb_t cb)
{
    s_recv_cb = cb;
}

esp_err_t esp8266_loop(void)
{
    parse_ipd_frames();

    /* ===== 抓取 ESP8266 主动上报的非 +IPD 行（如 0,CLOSED / WIFI DISCONNECT 等） ===== */
    while (1) {
        uint16_t used = ring_used(&s_rx);
        if (used == 0) break;

        /* 找最近的 \r\n 起始 */
        uint16_t end = 0xFFFF;
        for (uint16_t i = 0; i < used; i++) {
            uint16_t idx = (s_rx.tail + i) & ESP_RXBUF_MASK;
            if (s_rx.buf[idx] == '\n') { end = i; break; }
        }
        if (end == 0xFFFF) break;

        /* 拷贝这一行 */
        char line[128] = {0};
        uint16_t copy_len = (end + 1) < sizeof(line) ? (end + 1) : (sizeof(line) - 1);
        ring_copy_out(&s_rx, (uint8_t *)line, copy_len);
        line[copy_len] = 0;
        /* 去 \r\n */
        for (int k = (int)copy_len - 1; k >= 0; k--) {
            if (line[k] == '\r' || line[k] == '\n') line[k] = 0;
            else break;
        }

        /* 只打印有意义的行（避开裸 OK） */
        if (line[0] && strncmp(line, "OK", 2) != 0 && strncmp(line, ">", 1) != 0) {
            printf("[URC] %s\r\n", line);
        }

        /* 关键：解析 URC 同步 link 状态。
         * ESP8266 上报的格式：
         *   "0,CONNECTED" / "0,CLOSED"
         *   "WIFI DISCONNECT" / "WIFI CONNECTED"
         */
        /* 兼容 CIPMUX=0 ("CLOSED") 和 CIPMUX=1 ("0,CLOSED") */
        if (line[0] >= '0' && line[0] <= '4' && line[1] == ',') {
            /* CIPMUX=1：带 link_id 前缀 */
            int id = line[0] - '0';
            if (strstr(line, "CONNECTED") != NULL) {
                if (id < ESP_MAX_LINK_ID) s_link_state[id] = 1;
            } else if (strstr(line, "CLOSED") != NULL) {
                if (id < ESP_MAX_LINK_ID) s_link_state[id] = 0;
            }
        } else if (strcmp(line, "CLOSED") == 0 || strstr(line, "CLOSED\r\n") != NULL) {
            /* CIPMUX=0：单连接，只有 "CLOSED" 无 link_id */
            s_link_state[0] = 0;
        } else if (strstr(line, "WIFI DISCONNECT") != NULL) {
            s_wifi_connected = 0;
            memset(s_link_state, 0, sizeof(s_link_state));
        } else if (strstr(line, "WIFI CONNECTED") != NULL ||
                   strstr(line, "WIFI GOT IP")    != NULL) {
            s_wifi_connected = 1;
        }
    }
    return ESP_OK;
}

esp_err_t esp8266_connect_wifi(const char *ssid, const char *pwd)
{
    char cmd[128];
    char line[201];

#if ESP8266_USE_STORED_AP
    /* 使用 ESP8266 flash 里已烧录的 AP（上电自动连接）。
     * 这里不发送 AT+CWJAP，直接询问当前连接状态。
     */
    (void)ssid; (void)pwd;

    ring_consume(&s_rx, ring_used(&s_rx));   /* 清缓冲 */

    /* 1) 问 DHCP 状态 — 拿到 IP 就算连上了 */
    at_send_line("AT+CIPSTA?");
    if (at_wait_for("+CIPSTA:", 5000, line) == 0) {
        /* 进一步确认：找 station is connected AP 信息，AT+CWJAP? 拿当前连接 */
        ring_consume(&s_rx, ring_used(&s_rx));
        at_send_line("AT+CWJAP?");
        if (at_wait_for("+CWJAP:", 5000, line) == 0) {
            /* 例如回 "+CWJAP:"ssid","11:22:33:44:55:66",6,-45" */
            printf("[ESP] STORED AP active: %s\r\n", line);
            s_wifi_connected = 1;
            return ESP_OK;
        }
        /* 有 +CIPSTA 但没有 +CWJAP，也按成功处理（有 IP 即视为连上） */
        printf("[ESP] has IP, treating as connected\r\n");
        s_wifi_connected = 1;
        return ESP_OK;
    }

    /* 还没连上，等最多 30 秒（实测家庭网络握手 ~30s） */
    if (at_wait_for("WIFI GOT IP", 30000, line) == 0) {
        printf("[ESP] STORED AP got IP\r\n");
        s_wifi_connected = 1;
        return ESP_OK;
    }

    printf("[ESP] no IP from stored AP (flash empty?)\r\n");
    s_wifi_connected = 0;
    return ESP_ERR_TIMEOUT;
#else
    /* 原代码：发 AT+CWJAP，用代码里的 ssid/pwd 连接 */
    if (!ssid || !pwd) return ESP_ERR_PARAM;

    ring_consume(&s_rx, ring_used(&s_rx));

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    at_send_line(cmd);

    if (at_wait_for("+CWJAP:", 30000, line) == 0) {
        const char *colon = NULL;
        for (const char *q = line; *q; q++) {
            if (*q == ':') colon = q;
        }
        if (colon) {
            int err = atoi(colon + 1);
            const char *msg = "?";
            switch (err) {
                case 1: msg = "timeout";      break;
                case 2: msg = "wrong pwd";    break;
                case 3: msg = "AP not found"; break;
                case 4: msg = "connect fail"; break;
                default: msg = "unknown";     break;
            }
            printf("[ESP] CWJAP err=%d -> %s (raw=\"%s\", ssid=%s)\r\n",
                   err, msg, line, ssid);
        } else {
            printf("[ESP] CWJAP hit but no colon (raw=\"%s\")\r\n", line);
        }
        s_wifi_connected = 0;
        return ESP_ERR_RESP;
    }

    if (at_wait_for("WIFI GOT IP", 8000, line) == 0) {
        if (at_wait_for("OK", 2000, NULL) == 0) {
            s_wifi_connected = 1;
            return ESP_OK;
        }
        s_wifi_connected = 1;
        return ESP_OK;
    }

    printf("[ESP] CWJAP no reply at all (ssid=%s)\r\n", ssid);
    s_wifi_connected = 0;
    return ESP_ERR_TIMEOUT;
#endif
}

esp_err_t esp8266_disconnect_wifi(void)
{
    char line[201];
    at_send_line("AT+CWQAP");
    if (at_wait_for("OK", 3000, line) != 0) return ESP_ERR_RESP;
    s_wifi_connected = 0;
    return ESP_OK;
}

uint8_t esp8266_wifi_is_connected(void)
{
    return s_wifi_connected;
}

int8_t esp8266_get_rssi(void)
{
    return -127;
}

int8_t esp8266_tcp_connect(const char *host, uint16_t port)
{
    char cmd[128];
    char line[201];

    if (!host || !s_wifi_connected) return -1;

    ring_consume(&s_rx, ring_used(&s_rx));
    /* 单连接模式：不带 link_id 前缀 */
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
    at_send_line(cmd);

    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 15000) {
        // 检查 0,CONNECTED（旧固件）
        if (at_wait_for("0,CONNECTED", 200, line) == 0) {
            printf("[ESP] TCP 0,CONNECTED OK\n");
            s_link_state[0] = 1;
            return 0;
        }
        // 检查 CONNECT（新固件/简化固件）
        if (at_wait_for("CONNECT", 200, line) == 0) {
            printf("[ESP] TCP CONNECT OK\n");
            s_link_state[0] = 1;
            return 0;
        }
        if (at_wait_for("OK", 100, line) == 0) {
            // OK 之后可能马上来 CONNECT，继续等
            continue;
        }
        if (at_wait_for("ERROR", 100, line) == 0) {
            printf("[ESP] CIPSTART ERROR\n");
            return -1;
        }
        if (at_wait_for("0,CLOSED", 100, line) == 0) {
            printf("[ESP] TCP 0,CLOSED (connect failed)\n");
            return -1;
        }
    }
    printf("[ESP] TCP CONNECT timeout\n");
    return -1;
}

esp_err_t esp8266_tcp_close(int8_t link_id)
{
    char cmd[32];
    char line[201];

    if (link_id < 0 || link_id >= ESP_MAX_LINK_ID) return ESP_ERR_PARAM;

    /* CIPMUX=0 单连接模式：发送 AT+CIPCLOSE，不带 link_id */
    (void)link_id;
    at_send_line("AT+CIPCLOSE");
    if (at_wait_for("OK", 3000, line) != 0) return ESP_ERR_RESP;
    s_link_state[0] = 0;
    return ESP_OK;
}

/* 查询 ESP8266 内部 TCP 连接状态，主动打印 STATUS: 行 */
esp_err_t esp8266_query_tcp_status(int8_t link_id)
{
    char cmd[32];
    char line[201];

    if (link_id < 0 || link_id >= ESP_MAX_LINK_ID) return ESP_ERR_PARAM;

    ring_consume(&s_rx, ring_used(&s_rx));
    snprintf(cmd, sizeof(cmd), "AT+CIPSTATUS=%d", (int)link_id);
    at_send_line(cmd);
    if (at_wait_for("STATUS:", 2000, line) == 0) {
        printf("[TCP] status link=%d -> %s\r\n", (int)link_id, line);
    } else {
        printf("[TCP] status link=%d -> no reply (timeout)\r\n", (int)link_id);
    }
    (void)at_wait_for("OK", 1000, line);
    return ESP_OK;
}

/* 打印 ESP8266 当前 IP / 网关 / DNS / 子网掩码
 * 命令: AT+CIPSTA?
 * 响应: +CIPSTA:ip,"x.x.x.x"
 *      +CIPSTA:gateway,"x.x.x.x"
 *      +CIPSTA:netmask,"x.x.x.x"
 *      +CIPSTA:dns,"x.x.x.x"
 *      OK
 */
void esp8266_print_netinfo(void)
{
    char line[201];
    printf("[NET] >>> query IP info (AT+CIFSR) <<<\r\n");
    ring_consume(&s_rx, ring_used(&s_rx));
    at_send_line("AT+CIFSR");
    {
        uint32_t t0 = HAL_GetTick();
        while ((HAL_GetTick() - t0) < 3000) {
            /* 嗅探任何一行 */
            if (at_wait_for("+", 200, line) == 0) {
                printf("[NET] CIFSR: %s\r\n", line);
                continue;
            }
            if (at_wait_for("OK", 100, line) == 0) {
                printf("[NET] CIFSR done\r\n");
                break;
            }
        }
    }

    printf("[NET] >>> query DHCP info (AT+CIPSTA?) <<<\r\n");
    ring_consume(&s_rx, ring_used(&s_rx));
    at_send_line("AT+CIPSTA?");
    {
        uint32_t t0 = HAL_GetTick();
        while ((HAL_GetTick() - t0) < 3000) {
            if (at_wait_for("+", 200, line) == 0) {
                printf("[NET] CIPSTA: %s\r\n", line);
                continue;
            }
            if (at_wait_for("OK", 100, line) == 0) {
                printf("[NET] CIPSTA done\r\n");
                break;
            }
        }
    }
}

/* 同步 PING：ESP8266 内部 ping 一个域名/IP，回 +PONG 或 +timeout */
esp_err_t esp8266_ping(const char *host)
{
    char cmd[64];
    char line[201];

    if (!host) return ESP_ERR_PARAM;

    ring_consume(&s_rx, ring_used(&s_rx));
    snprintf(cmd, sizeof(cmd), "AT+PING=\"%s\"", host);
    at_send_line(cmd);

    /* AT+PING 响应（新固件）:
     *   主机在: +<time> ms
     *            OK
     * 老固件:    +PONG
     *            OK
     * 超时:      +timeout
     *            ERROR 或 FAIL
     *
     * 策略：等 "OK" 作为命令终结点，回看之前有没有 +<time> 或 +PONG
     */
    uint32_t t0 = HAL_GetTick();
    int pong_found = 0;
    int last_was_pong_or_time = 0;

    while ((HAL_GetTick() - t0) < 35000) {
        /* 嗅探一行 */
        if (at_wait_for("+", 200, line) == 0) {
            /* 取出这一行（已去掉头部 +）*/
            /* line 是 at_wait_for 拷贝的，包含 +... 但没有 \r\n */
            printf("[PING] %s -> URC: %s\r\n", host, line);

            /* +<数字>      = ping 成功，<数字> 是毫秒 */
            /* +timeout     = ping 失败 */
            /* +PONG        = 老固件成功 */
            if (line[1] >= '0' && line[1] <= '9' && line[1] != 't') {
                /* 数字应答时间 */
                pong_found = 1;
                last_was_pong_or_time = 1;
            } else if (strstr(line, "PONG") != NULL) {
                pong_found = 1;
                last_was_pong_or_time = 1;
            } else if (strstr(line, "timeout") != NULL) {
                printf("[PING] %s -> TIMEOUT (unreachable)\r\n", host);
                (void)at_wait_for("OK", 2000, line);
                return ESP_ERR_RESP;
            } else {
                last_was_pong_or_time = 0;
            }
            continue;
        }

        /* 看到 OK：命令结束。根据前面是否拿到 +time / +PONG 判断 */
        if (at_wait_for("OK", 100, line) == 0) {
            if (pong_found) {
                printf("[PING] %s -> SUCCESS\r\n", host);
                return ESP_OK;
            } else {
                printf("[PING] %s -> FAIL (no +time, no +PONG)\r\n", host);
                return ESP_ERR_RESP;
            }
        }

        /* 命令失败 */
        if (at_wait_for("ERROR", 50, line) == 0) {
            printf("[PING] %s -> ERROR\r\n", host);
            return ESP_ERR_RESP;
        }
    }
    printf("[PING] %s -> no reply (35s timeout)\r\n", host);
    return ESP_ERR_TIMEOUT;
}

esp_err_t esp8266_tcp_send(int8_t link_id, const uint8_t *data, uint16_t len)
{
    char cmd[32];
    char line[201];

    if (link_id < 0 || link_id >= ESP_MAX_LINK_ID) return ESP_ERR_PARAM;
    if (!data || len == 0) return ESP_ERR_PARAM;

    /* 1) 声明长度（CIPMUX=0 单连接模式：不带 link_id） */
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", len);
    at_send_line(cmd);
    if (at_wait_for(">", 2000, line) != 0) return ESP_ERR_NO_RESP;

    /* 2) 发送数据 */
    uart_send(data, len);
    if (at_wait_for("SEND OK", 5000, line) != 0) return ESP_ERR_RESP;
    return ESP_OK;
}

uint8_t esp8266_is_link_up(int8_t link_id)
{
    if (link_id < 0 || link_id >= ESP_MAX_LINK_ID) return 0;
    return s_link_state[link_id];
}


