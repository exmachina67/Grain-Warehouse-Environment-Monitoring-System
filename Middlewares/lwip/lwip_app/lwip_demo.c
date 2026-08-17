/**
 ****************************************************************************************************
 * @file        lwip_demo
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-08-01
 * @brief       lwIP+Aliyun+MQTT实验
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 探索者 F407开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */
 
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdint.h>
#include <stdio.h>
#include <netdb.h>
#include "lwip/apps/mqtt.h"
#include "./BSP/LCD/lcd.h"
#include "./MALLOC/malloc.h"
#include "./BSP/LED/led.h"
#include "lwip_demo.h"
#include "hmac.h"
#include "string.h"


/* oneNET参考文章：https://open.iot.10086.cn/doc/v5/develop/detail/251 */

//static const struct mqtt_connect_client_info_t mqtt_client_info =
//{
//    "MQTT",     /* 设备名 */
//    "366007",   /* 产品ID */
//    "version=2018-10-31&res=products%2F366007%2Fdevices%2FMQTT&et=1672735919&method=md5&sign=qI0pgDJnICGoPdhNi%2BHtfg%3D%3D", /* pass */
//    100,  /* keep alive */
//    NULL, /* will_topic */
//    NULL, /* will_msg */
//    0,    /* will_qos */
//    0     /* will_retain */
//#if LWIP_ALTCP && LWIP_ALTCP_TLS  /* 加密操作，我们一般不使用加密操作 */
//  , NULL
//#endif
//};

static ip_addr_t g_mqtt_ip;
static mqtt_client_t* g_mqtt_client;
float g_temp = 0;    /* 温度值 */
float g_humid = 0;   /* 湿度值 */
unsigned char g_payload_out[200];
int g_payload_out_len = 0;
int g_publish_flag = 0;/* 发布成功标志位 */
void lwip_ali_get_password(const char *device_secret, const char *content, char *password);

/**
 * @brief       mqtt进入数据回调函数
 * @param       arg：传入的参数
 * @param       data：数据
 * @param       len：数据大小
 * @param       flags：标志
 * @retval      无
 */
static void 
mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    const struct mqtt_connect_client_info_t* client_info = (const struct mqtt_connect_client_info_t*)arg;
    LWIP_UNUSED_ARG(data);
    printf("\r\nMQTT client \"%s\" data cb: len %d, flags %d\n", client_info->client_id,(int)len, (int)flags);
    
    if (flags & MQTT_DATA_FLAG_LAST)
    {
        /* 从这里可以接收阿里云发布的数据 */ 
    }
}

/**
 * @brief       mqtt进入发布回调函数
 * @param       arg：传入的参数
 * @param       topic：主题
 * @param       tot_len：主题大小
 * @retval      无
 */
static void
mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    const struct mqtt_connect_client_info_t* client_info = (const struct mqtt_connect_client_info_t*)arg;

    printf("\r\nMQTT client \"%s\" publish cb: topic %s, len %d\r\n", client_info->client_id,
          topic, (int)tot_len);
}

/**
 * @brief       mqtt发布回调函数
 * @param       arg：传入的参数
 * @param       err：错误值
 * @retval      无
 */
static void
mqtt_publish_request_cb(void *arg, err_t err)
{
    printf("publish success\r\n");
}

/**
 * @brief       mqtt订阅响应回调函数
 * @param       arg：传入的参数
 * @param       err：错误值
 * @retval      无
 */
static void
mqtt_request_cb(void *arg, err_t err)
{
    const struct mqtt_connect_client_info_t* client_info = (const struct mqtt_connect_client_info_t*)arg;
    
    g_publish_flag = 1;
    printf("\r\nMQTT client \"%s\" request cb: err %d\r\n", client_info->client_id, (int)err);
}

/**
 * @brief       mqtt连接回调函数
 * @param       client：客户端控制块
 * @param       arg：传入的参数
 * @param       status：连接状态
 * @retval      无
 */
static void
mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    err_t err;
    
    const struct mqtt_connect_client_info_t* client_info = (const struct mqtt_connect_client_info_t*)arg;
    
    LWIP_UNUSED_ARG(client);

    printf("\r\nMQTT client \"%s\" connection cb: status %d\r\n", client_info->client_id, (int)status);

    /* 判断是否连接 */ 
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        /* 判断是否连接 */ 
        if (mqtt_client_is_connected(client))
        {
            /* 设置传入发布请求的回调 */
            mqtt_set_inpub_callback(g_mqtt_client,
                                    mqtt_incoming_publish_cb,
                                    mqtt_incoming_data_cb,
                                    NULL);
            
            /* 订阅操作，并设置订阅响应会回调函数mqtt_sub_request_cb */ 
            err = mqtt_subscribe(client, DEVICE_SUBSCRIBE, 1, mqtt_request_cb, arg);
            
            if(err == ERR_OK)
            {
                printf("mqtt_subscribe return: %d\n", err);
                lcd_show_string(5, 170, 210, 16, 16, "mqtt_subscribe succeed", BLUE);
            }

        }
    }
    else/* 连接失败 */
    {
        printf("mqtt_connection_cb: Disconnected, reason: %d\n", status);
    } 
}

/**
 * @brief       lwip_demo进程
 * @param       无
 * @retval      无
 */
void lwip_demo(void)
{
    struct hostent *server;
    static struct mqtt_connect_client_info_t mqtt_client_info;
    
    
    server = gethostbyname((char *)HOST_NAME);        /* 对oneNET服务器地址解析 */
    memcpy(&g_mqtt_ip,server->h_addr,server->h_length); /* 把解析好的地址存放在mqtt_ip变量当中 */
    
    char *PASSWORD;
    PASSWORD = mymalloc(SRAMIN, 300); /* 为密码申请内存 */
    lwip_ali_get_password(DEVICE_SECRET, CONTENT, PASSWORD);  /*  通过hmac_sha1算法得到password  */
    
    
    /* 设置一个空的客户端信息结构 */
    memset(&mqtt_client_info, 0, sizeof(mqtt_client_info));
    
    /* 设置客户端的信息量 */ 
    mqtt_client_info.client_id = (char *)CLIENT_ID;     /* 设备名称 */
    mqtt_client_info.client_user = (char *)USER_NAME;   /* 产品ID */
    mqtt_client_info.client_pass = (char *)PASSWORD;    /* 计算出来的密码 */
    mqtt_client_info.keep_alive = 100;                  /* 保活时间 */
    mqtt_client_info.will_msg = NULL;
    mqtt_client_info.will_qos = NULL;
    mqtt_client_info.will_retain = 0;
    mqtt_client_info.will_topic = 0;
    
    myfree(SRAMIN, PASSWORD);                 /* 释放内存 */
    
    /* 创建MQTT客户端控制块 */
    g_mqtt_client = mqtt_client_new();
    
    /* 连接服务器 */
    mqtt_client_connect(g_mqtt_client,        /* 服务器控制块 */
                        &g_mqtt_ip, MQTT_PORT,/* 服务器IP与端口号 */
                        mqtt_connection_cb, LWIP_CONST_CAST(void*, &mqtt_client_info),/* 设置服务器连接回调函数 */
                        &mqtt_client_info); /* MQTT连接信息 */
    while(1)
    {
        if (g_publish_flag == 1)
        {
            g_temp = 30 + rand() % 10 + 1;   /* 温度的数据 */
            g_humid = 54.8 + rand() % 10 + 1;/* 湿度的数据 */
            sprintf((char *)g_payload_out, "{\"params\":{\"CurrentTemperature\":+%0.1f,\"RelativeHumidity\":%0.1f},\"method\":\"thing.event.property.post\"}", g_temp, g_humid);
            g_payload_out_len = strlen((char *)g_payload_out);
            mqtt_publish(g_mqtt_client,DEVICE_PUBLISH,g_payload_out,g_payload_out_len,1,0,mqtt_publish_request_cb,NULL);
        }
        
        vTaskDelay(1000);
    }
}

/**
 * @brief      将16进制数转化为字符串
 * @param      pbSrc - 输入16进制数的起始地址
 * @param      nLen - 16进制数的字节数
 * @param       pbDest - 存放目标字符串
 * @retval     无
 */
void lwip_ali_hextostr(uint8_t *pbDest, uint8_t *pbSrc, int nLen)
{
    char ddl, ddh;
    int i;

    for (i = 0; i < nLen; i++)
    {
        ddh = 48 + pbSrc[i] / 16;
        ddl = 48 + pbSrc[i] % 16;

        if (ddh > 57) ddh = ddh + 7;

        if (ddl > 57) ddl = ddl + 7;

        pbDest[i * 2] = ddh;
        pbDest[i * 2 + 1] = ddl;
    }

    pbDest[nLen * 2] = '\0';
}

/**
 * @brief      通过hmac_sha1算法获取password
 * @param      device_secret---设备的密钥
 * @param      content --- 登录密码
 * @param      password---返回的密码值
 * @retval     无
 */
void lwip_ali_get_password(const char *device_secret, const char *content, char *password)
{
    char buf[256] = {0};
    int len = sizeof(buf);
    hmac_sha1((uint8_t *)device_secret, strlen(device_secret), (uint8_t *)content, strlen(content), (uint8_t *)buf, (unsigned int *)&len);
    lwip_ali_hextostr((uint8_t *)password, (uint8_t *)buf, len);
}

