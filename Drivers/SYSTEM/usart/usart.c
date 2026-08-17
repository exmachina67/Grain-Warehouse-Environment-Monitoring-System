/**
 ****************************************************************************************************
 * @file        usart.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.1
 * @date        2023-06-05
 * @brief       串口初始化代码(一般是串口1)，支持printf
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
 * 修改说明
 * V1.0 20211014
 * 第一次发布
 * V1.1 20230605
 * 删除USART_UX_IRQHandler()函数的超时处理和修改HAL_UART_RxCpltCallback()
 ****************************************************************************************************
 */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"


/* 如果使用os,则包括下面的头文件即可 */
//#if SYS_SUPPORT_OS
//#include "os.h"                               /* os 使用 */
//#endif

/******************************************************************************************/
/* 加入以下代码, 支持printf函数, 而不需要选择use MicroLIB */

#if 1
#if (__ARMCC_VERSION >= 6010050)                    /* 使用AC6编译器时 */
__asm(".global __use_no_semihosting\n\t");          /* 声明不使用半主机模式 */
__asm(".global __ARM_use_no_argv \n\t");            /* AC6下需要声明main函数为无参数格式，否则部分例程可能出现半主机模式 */

#else
/* 使用AC5编译器时, 要在这里定义__FILE 和 不使用半主机模式 */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
    /* Whatever you require here. If the only file you are using is */
    /* standard output using printf() for debugging, no file handling */
    /* is required. */
};

#endif

/* 不使用半主机模式，至少需要重定义_ttywrch\_sys_exit\_sys_command_string函数,以同时兼容AC6和AC5模式 */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

/* 定义_sys_exit()以避免使用半主机模式 */
void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

/* FILE 在 stdio.h里面定义. */
FILE __stdout;

/* 重定义fputc函数, printf函数最终会通过调用fputc输出字符串到串口 */
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);               /* 等待上一个字符发送完成 */

    USART1->DR = (uint8_t)ch;                       /* 将要发送的字符 ch 写入到DR寄存器 */
    return ch;
}
#endif
/***********************************************END*******************************************/
    
#if USART_EN_RX                                     /* 如果使能了接收 */

/* 接收缓冲, 最大USART_REC_LEN个字节. */
uint8_t g_usart_rx_buf[USART_REC_LEN];
uint8_t g_usart2_rx_buf[USART_REC_LEN];

/*  接收状态
 *  bit15，      接收完成标志
 *  bit14，      接收到0x0d
 *  bit13~0，    接收到的有效字节数目
*/
uint16_t g_usart_rx_sta = 0;
uint16_t g_usart2_rx_sta = 0;

uint8_t g_rx_buffer[RXBUFFERSIZE];                  /* HAL库使用的串口接收缓冲 */
uint8_t g_uart2_rx_buffer[RXBUFFERSIZE];

UART_HandleTypeDef g_uart1_handle;                  /* UART句柄 */
UART_HandleTypeDef g_uart2_handle;

uint8_t my_rx_buf[USART_REC_LEN]; // ??????????
uint16_t my_rx_index = 0; 


/**
 * @brief       串口X初始化函数
 * @param       baudrate: 波特率, 根据自己需要设置波特率值
 * @note        注意: 必须设置正确的时钟源, 否则串口波特率就会设置异常.
 *              这里的USART的时钟源在sys_stm32_clock_init()函数中已经设置过了.
 * @retval      无
 */
void usart_init(uint32_t baudrate)
{
    g_uart1_handle.Instance = USART_UX;                         /* USART1 */
    g_uart1_handle.Init.BaudRate = baudrate;                    /* 波特率 */
    g_uart1_handle.Init.WordLength = UART_WORDLENGTH_8B;        /* 字长为8位数据格式 */
    g_uart1_handle.Init.StopBits = UART_STOPBITS_1;             /* 一个停止位 */
    g_uart1_handle.Init.Parity = UART_PARITY_NONE;              /* 无奇偶校验位 */
    g_uart1_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;        /* 无硬件流控 */
    g_uart1_handle.Init.Mode = UART_MODE_TX_RX;                 /* 收发模式 */
    HAL_UART_Init(&g_uart1_handle);                             /* HAL_UART_Init()会使能UART1 */
    
    /* ========== USART2 初始化（新增） ========== */
    g_uart2_handle.Instance = USART2;                           /* USART2 */
    g_uart2_handle.Init.BaudRate = baudrate;                    /* 波特率 */
    g_uart2_handle.Init.WordLength = UART_WORDLENGTH_8B;        /* 字长为8位数据格式 */
    g_uart2_handle.Init.StopBits = UART_STOPBITS_1;             /* 一个停止位 */
    g_uart2_handle.Init.Parity = UART_PARITY_NONE;              /* 无奇偶校验位 */
    g_uart2_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;        /* 无硬件流控 */
    g_uart2_handle.Init.Mode = UART_MODE_TX_RX;                 /* 收发模式 */
    HAL_UART_Init(&g_uart2_handle);                             /* HAL_UART_Init()会使能UART2 */
    
    HAL_UART_Receive_IT(&g_uart2_handle, (uint8_t *)g_uart2_rx_buffer, RXBUFFERSIZE);
    
    /* 该函数会开启接收中断：标志位UART_IT_RXNE，并且设置接收缓冲以及接收缓冲接收最大数据量 */
    HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
}

/**
 * @brief       UART底层初始化函数
 * @param       huart: UART句柄类型指针
 * @note        此函数会被HAL_UART_Init()调用
 *              完成时钟使能，引脚配置，中断配置
 * @retval      无
 */
//void HAL_UART_MspInit(UART_HandleTypeDef *huart)
//{
//    GPIO_InitTypeDef gpio_init_struct;
//    if(huart->Instance == USART_UX)                             /* 如果是串口1，进行串口1 MSP初始化 */
//    {
//        USART_UX_CLK_ENABLE();                                  /* USART1 时钟使能 */
//        USART_TX_GPIO_CLK_ENABLE();                             /* 发送引脚时钟使能 */
//        USART_RX_GPIO_CLK_ENABLE();                             /* 接收引脚时钟使能 */

//        gpio_init_struct.Pin = USART_TX_GPIO_PIN;               /* TX引脚 */
//        gpio_init_struct.Mode = GPIO_MODE_AF_PP;                /* 复用推挽输出 */
//        gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
//        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;          /* 高速 */
//        gpio_init_struct.Alternate = USART_TX_GPIO_AF;          /* 复用为USART1 */
//        HAL_GPIO_Init(USART_TX_GPIO_PORT, &gpio_init_struct);   /* 初始化发送引脚 */

//        gpio_init_struct.Pin = USART_RX_GPIO_PIN;               /* RX引脚 */
//        gpio_init_struct.Alternate = USART_RX_GPIO_AF;          /* 复用为USART1 */
//        HAL_GPIO_Init(USART_RX_GPIO_PORT, &gpio_init_struct);   /* 初始化接收引脚 */

//#if USART_EN_RX
//        HAL_NVIC_EnableIRQ(USART_UX_IRQn);                      /* 使能USART1中断通道 */
//        HAL_NVIC_SetPriority(USART_UX_IRQn, 3, 3);              /* 抢占优先级3，子优先级3 */
//#endif
//    }
//}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init_struct;
    
    /* ========== USART1 MSP 初始化 ========== */
    if(huart->Instance == USART_UX)                             /* 如果是串口1，进行串口1 MSP初始化 */
    {
        USART_UX_CLK_ENABLE();                                  /* USART1 时钟使能 */
        USART_TX_GPIO_CLK_ENABLE();                             /* 发送引脚时钟使能 */
        USART_RX_GPIO_CLK_ENABLE();                             /* 接收引脚时钟使能 */

        gpio_init_struct.Pin = USART_TX_GPIO_PIN;               /* TX引脚 */
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;                /* 复用推挽输出 */
        gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;          /* 高速 */
        gpio_init_struct.Alternate = USART_TX_GPIO_AF;          /* 复用为USART1 */
        HAL_GPIO_Init(USART_TX_GPIO_PORT, &gpio_init_struct);   /* 初始化发送引脚 */

        gpio_init_struct.Pin = USART_RX_GPIO_PIN;               /* RX引脚 */
        gpio_init_struct.Alternate = USART_RX_GPIO_AF;          /* 复用为USART1 */
        HAL_GPIO_Init(USART_RX_GPIO_PORT, &gpio_init_struct);   /* 初始化接收引脚 */

#if USART_EN_RX
        HAL_NVIC_EnableIRQ(USART_UX_IRQn);                      /* 使能USART1中断通道 */
        HAL_NVIC_SetPriority(USART_UX_IRQn, 3, 3);              /* 抢占优先级3，子优先级3 */
#endif
    }
    
    /* ========== USART2 MSP 初始化（新增） ========== */
    if(huart->Instance == USART2)                               /* 如果是串口2，进行串口2 MSP初始化 */
    {
        USART_U2_CLK_ENABLE();                                  /* USART2 时钟使能 */
        USART2_TX_GPIO_CLK_ENABLE();                            /* 发送引脚时钟使能 */
        USART2_RX_GPIO_CLK_ENABLE();                            /* 接收引脚时钟使能 */

        gpio_init_struct.Pin = USART2_TX_GPIO_PIN;              /* TX引脚 PA2 */
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;                /* 复用推挽输出 */
        gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;          /* 高速 */
        gpio_init_struct.Alternate = USART2_TX_GPIO_AF;         /* 复用为USART2 */
        HAL_GPIO_Init(USART2_TX_GPIO_PORT, &gpio_init_struct);  /* 初始化发送引脚 */

        gpio_init_struct.Pin = USART2_RX_GPIO_PIN;              /* RX引脚 PA3 */
        gpio_init_struct.Alternate = USART2_RX_GPIO_AF;         /* 复用为USART2 */
        HAL_GPIO_Init(USART2_RX_GPIO_PORT, &gpio_init_struct);  /* 初始化接收引脚 */

#if USART_EN_RX
        HAL_NVIC_EnableIRQ(USART2_IRQn);                        /* 使能USART2中断通道 */
        HAL_NVIC_SetPriority(USART2_IRQn, 3, 2);                /* 抢占优先级3，子优先级3 */
#endif
    }else if(huart->Instance==USART3) // 如果是串口3，用于ATK-MW1278D模块
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();           // 使能GPIOB时钟
        __HAL_RCC_USART3_CLK_ENABLE();          // 使能USART3时钟

        gpio_init_struct.Pin=GPIO_PIN_10;           // PB10
        gpio_init_struct.Mode=GPIO_MODE_AF_PP;      // 复用推挽输出
        gpio_init_struct.Pull=GPIO_PULLUP;          // 上拉
        gpio_init_struct.Speed=GPIO_SPEED_FAST;     // 高速
        gpio_init_struct.Alternate=GPIO_AF7_USART3; // 复用为USART3
        HAL_GPIO_Init(GPIOB,&gpio_init_struct);     // 初始化PB10

        gpio_init_struct.Pin=GPIO_PIN_11;           // PB11
        HAL_GPIO_Init(GPIOB,&gpio_init_struct);     // 初始化PB11

        HAL_NVIC_EnableIRQ(USART3_IRQn);        // 使能USART3中断通道
        HAL_NVIC_SetPriority(USART3_IRQn,2,2);  // 抢占优先级2，子优先级2
    }
}

/**
 * @brief       Rx传输回调函数
 * @param       huart: UART句柄类型指针
 * @retval      无
 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if(huart->Instance==USART2)//????????1
	{
        // USART2接收处理 - 按照\r\n逻辑处理
        if((g_usart2_rx_sta & 0x8000) == 0)      /* 接收未完成 */
        {
            if(g_usart2_rx_sta & 0x4000)         /* 接收到了0x0d */
            {
                if(g_uart2_rx_buffer[0] != 0x0a) 
                {
                    g_usart2_rx_sta = 0;         /* 接收错误,重新开始 */
                }
                else 
                {
                    g_usart2_rx_sta |= 0x8000;   /* 接收完成了 */
                }
            }
            else                                /* 还没收到0X0D */
            {
                if(g_uart2_rx_buffer[0] == 0x0d)
                {
                    g_usart2_rx_sta |= 0x4000;
                }
                else
                {
                    g_usart2_rx_buf[g_usart2_rx_sta & 0X3FFF] = g_uart2_rx_buffer[0];
                    g_usart2_rx_sta++;
                    if(g_usart2_rx_sta > (USART_REC_LEN - 1))
                    {
                        g_usart2_rx_sta = 0;     /* 接收数据错误,重新开始接收 */
                    }
                }
            }
        }
        HAL_UART_Receive_IT(&g_uart2_handle, g_uart2_rx_buffer, 1);
    }

}

void USART1_IRQHandler(void)  // 修改为正确的中断函数名
{
	uint8_t res;
	if((__HAL_UART_GET_FLAG(&g_uart1_handle,UART_FLAG_RXNE)!=RESET))
	{
        res = USART1->DR;
        
        if(res == '#') // ??#?????
        {
            my_rx_index = 0;
            my_rx_buf[0] = 0;
        }
        else
        {
            my_rx_buf[my_rx_index++] = res;
            if(my_rx_index >= USART_REC_LEN - 1) my_rx_index = 0;
        }
        
        
//        my_rx_buf[my_rx_index++] = res;
//        if(my_rx_index >= MY_BUF_LEN - 1) my_rx_index = 0; // ????
		
//		if((USART_RX_STA&0x8000)==0)
//		{
//			if(USART_RX_STA&0x4000)
//			{
//				if(res!=0x0a)USART_RX_STA=0;
//				else USART_RX_STA|=0x8000;
//			}
//			else
//			{
//				if(res==0x0d)USART_RX_STA|=0x4000;
//				else
//				{
//					USART_RX_BUF[USART_RX_STA&0X3FFF]=res;
//					USART_RX_STA++;
//					if(USART_RX_STA>(USART_REC_LEN-1))USART_RX_STA=0;
//				}
//			}
//		}
	}
	HAL_UART_IRQHandler(&g_uart1_handle);
} 
//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//    if(huart->Instance == USART_UX)             /* 如果是串口1 */
//    {
//        if((g_usart_rx_sta & 0x8000) == 0)      /* 接收未完成 */
//        {
//            if(g_usart_rx_sta & 0x4000)         /* 接收到了0x0d */
//            {
//                if(g_rx_buffer[0] != 0x0a) 
//                {
//                    g_usart_rx_sta = 0;         /* 接收错误,重新开始 */
//                }
//                else 
//                {
//                    g_usart_rx_sta |= 0x8000;   /* 接收完成了 */
//                }
//            }
//            else                                /* 还没收到0X0D */
//            {
//                if(g_rx_buffer[0] == 0x0d)
//                {
//                    g_usart_rx_sta |= 0x4000;
//                }
//                else
//                {
//                    g_usart_rx_buf[g_usart_rx_sta & 0X3FFF] = g_rx_buffer[0] ;
//                    g_usart_rx_sta++;
//                    if(g_usart_rx_sta > (USART_REC_LEN - 1))
//                    {
//                        g_usart_rx_sta = 0;     /* 接收数据错误,重新开始接收 */
//                    }
//                }
//            }
//        }
//        
//        HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
//    }
//}

/**
 * @brief       串口1中断服务函数
 * @param       无
 * @retval      无
 */
//void USART_UX_IRQHandler(void)
//{ 
////#if SYS_SUPPORT_OS                              /* 使用OS */
////    OSIntEnter();    
////#endif

//    HAL_UART_IRQHandler(&g_uart1_handle);       /* 调用HAL库中断处理公用函数 */

////#if SYS_SUPPORT_OS                              /* 使用OS */
////    OSIntExit();
////#endif
//}
void USART2_IRQHandler(void)
{ 
//#if SYS_SUPPORT_OS                              /* 使用OS */
//    OSIntEnter();    
//#endif

    HAL_UART_IRQHandler(&g_uart2_handle);       /* 调用HAL库中断处理公用函数 */

//#if SYS_SUPPORT_OS                              /* 使用OS */
//    OSIntExit();
//#endif
}


void usart_send_data(USART_Select_t usart_num, uint8_t *data, uint16_t len)
{
    switch(usart_num)
    {
        case USART_1:
            HAL_UART_Transmit(&g_uart1_handle, data, len, 1000);
            break;
        case USART_2:
            HAL_UART_Transmit(&g_uart2_handle, data, len, 1000);
            break;
        case USART_3:
//            HAL_UART_Transmit(&g_uart3_handle, data, len, 1000);
            break;
        default:
            break;
    }
}

#endif	



 

 




