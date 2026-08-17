#include "./BSP/ATK-MW1278/atk-mw1278-run.h"
#include "./BSP/ATK-MW1278/atk_mw1278d.h"
#include "stm32f4xx.h"                  // Device header
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/led/led.h"

/* ATK-MW1278DÄ£¿éÅäÖÃ²ÎÊý¶¨Òå */
#define DEMO_ADDR       0                               /* Éè±¸µØÖ· */
#define DEMO_WLRATE     ATK_MW1278D_WLRATE_19K2         /* ¿ÕÖÐËÙÂÊ */
#define DEMO_CHANNEL    0                               /* ÐÅµÀ */
#define DEMO_TPOWER     ATK_MW1278D_TPOWER_20DBM        /* ·¢Éä¹¦ÂÊ */
#define DEMO_WORKMODE   ATK_MW1278D_WORKMODE_NORMAL     /* ¹¤×÷Ä£Ê½ */
#define DEMO_TMODE      ATK_MW1278D_TMODE_TT            /* ·¢ÉäÄ£Ê½ */
#define DEMO_WLTIME     ATK_MW1278D_WLTIME_1S           /* ÐÝÃßÊ±¼ä */
#define DEMO_UARTRATE   ATK_MW1278D_UARTRATE_115200BPS  /* UARTÍ¨Ñ¶²¨ÌØÂÊ */
#define DEMO_UARTPARI   ATK_MW1278D_UARTPARI_NONE       /* UARTÍ¨Ñ¶Ð£ÑéÎ» */

void atk_mw1278_init(void)
{
    uint8_t ret;
    
    ret = atk_mw1278d_init(115200);
    if (ret != 0)
    {
        printf("ATK-MW1278D init failed!\r\n");
        while (1)
        {
//            LED0_TOGGLE();
            delay_ms(200);
        }
    }
    
    /* ����ATK-MW1278Dģ�� */
    atk_mw1278d_enter_config();
    ret  = atk_mw1278d_addr_config(DEMO_ADDR);
    ret += atk_mw1278d_wlrate_channel_config(DEMO_WLRATE, DEMO_CHANNEL);
    ret += atk_mw1278d_tpower_config(DEMO_TPOWER);
    ret += atk_mw1278d_workmode_config(DEMO_WORKMODE);
    ret += atk_mw1278d_tmode_config(DEMO_TMODE);
    ret += atk_mw1278d_wltime_config(DEMO_WLTIME);
    ret += atk_mw1278d_uart_config(DEMO_UARTRATE, DEMO_UARTPARI);
    atk_mw1278d_exit_config();
    if (ret != 0)
    {
        printf("ATK-MW1278D config failed!\r\n");
        while (1)
        {
//            LED0_TOGGLE();
            delay_ms(200);
        }
    }
    
    printf("ATK-MW1278D config succedded!\r\n");
    atk_mw1278d_uart_rx_restart();
    
}

void atk_mw1278_send(uint16_t temp, uint16_t humi, uint32_t CO, uint32_t CO2, uint8_t dry)
{
            if (atk_mw1278d_free() != ATK_MW1278D_EBUSY)
            {
                atk_mw1278d_uart_printf("%d, %d, %d, %d, %d\r\n", temp, humi, CO, CO2, dry);
            }
}


