/**
 ****************************************************************************************************
 * @file        atk_tof.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2024-11-01
 * @brief       ATK_TOF模块 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 M48Z-M3最小系统板STM32F103版
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __ATK_TOF_H
#define __ATK_TOF_H

#include "stdint.h"
#include "stdio.h"
#include "./SYSTEM/SYS/sys.h"
#include "./BSP/IIC/myiic.h"
#include "./SYSTEM/DELAY/delay.h"


/**************************************************************************************************/
/* XS引脚定义 */

#define ATK_TOF_XSH_GPIO_PORT            GPIOB
#define ATK_TOF_XSH_GPIO_PIN             GPIO_PIN_4
#define ATK_TOF_XSH_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)

/* IO操作 */
#define ATK_TOF_XSH(x)                   do{ x ?                                                                                  \
                                                HAL_GPIO_WritePin(ATK_TOF_XSH_GPIO_PORT, ATK_TOF_XSH_GPIO_PIN, GPIO_PIN_SET) :    \
                                                HAL_GPIO_WritePin(ATK_TOF_XSH_GPIO_PORT, ATK_TOF_XSH_GPIO_PIN, GPIO_PIN_RESET);   \
                                         }while(0)

/* ATK-TOF模块上电默认IIC通讯地址 */
#define ATK_TOF_IIC_ADDR          0xD8

/* ATK-TOF模块状态值 */
typedef enum 
{
    VI530x_OK                   = 0x00,
    VI530x_RANGING              = 0x01,
    VI530x_BUSY                 = 0x02,
    VI530x_SLEEP                = 0x04,
    VI530x_IIC_ERROR            = 0x08,
    VI530x_ERROR                = 0x10,
    VI530x_ERROR_XTALK_CALIB    = 0x20,
    VI5300_ERROR_OFFSET_CALIB   = 0x40,
    VI530x_ERROR_FW_FAILURE     = 0x80
} VI530x_Status;

/* ATK-TOF模块读取模式
 * 0：单次读取
 * 1：连续读取
 */
#define CONTINUOUS_READ           1

/**************************************************************************************************/

void atk_tof_hw_reset(void);                                                          /* ATK-TOF模块硬件复位 */
void atk_tof_hw_init(void);                                                           /* ATK-TOF模块硬件初始化 */
VI530x_Status atk_tof_write_byte(uint8_t reg_addr, uint8_t data);                     /* 写一个字节数据 */
VI530x_Status atk_tof_write_nbytes(uint8_t reg_addr, uint8_t *data, uint16_t len);    /* 写N个字节数据 */
VI530x_Status atk_tof_read_byte(uint8_t reg_addr, uint8_t *data);                     /* 读一个字节数据 */
VI530x_Status atk_tof_read_nbytes(uint8_t reg_addr, uint8_t *data, uint16_t len);     /* 读N个字节数据 */

#endif





