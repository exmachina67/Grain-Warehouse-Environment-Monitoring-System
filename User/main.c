
#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/KEY/key.h"
#include "./BSP/BEEP/beep.h"
#include "./BSP/MQ7/mq7.h"
#include "./BSP/SRAM/sram.h"
#include "./BSP/ADC/adc.h"
#include "./BSP/FC37/fc37.h"
#include "./BSP/DHT11/dht11.h"
#include "./BSP/MQ135/mq135.h"
#include "./BSP/motor/gtim.h"
#include "./BSP/ST021/st021.h"
#include "./BSP/ATK-MW1278/atk_mw1278d.h"
#include "./BSP/ATK-MW1278/atk_mw1278d_uart.h"
#include "./BSP/RTC/rtc.h"
#include "./BSP/ATK_TOF/atk_tof.h"
#include "./BSP/ATK_TOF/vi5300/VI530x_API.h"
#include "./BSP/LIGHT/light.h"
#include "./MALLOC/malloc.h"
#include "FreeOS_app.h"

uint8_t lora_ready = 0;

int main(void)
{
    HAL_Init();                         
    sys_stm32_clock_init(336, 8, 2, 7); 
    delay_init(168);                    
    usart_init(115200);
    /* 测试阶段关闭独立看门狗，避免长操作触发复位导致死循环。
     * 等所有网络/MQTT 跑通后再加回来。 */
    //iwdg_init(IWDG_PRESCALER_256, 2500);   /* 预分频数256,重载值2500,溢出时间Tout=(8*2500)=20000ms=20s */
    
    /* 外设初始化 */
    led_init();
    key_init();
    sram_init();
    my_mem_init(SRAMIN);
    beep_init();
    adc_init();
    light_init();
    gtim_timx_pwm_chy_init(300 - 1, 168 - 1);  /* PWM初始化，周期300*168/168MHz = 0.3ms，占空比范围0~300 */

    /* 传感器初始化 */
    DHT11_Init();
    FC37_Init();
    MQ135_Init();
    MQ7_Init();
    ST021_Init();
    lora_ready = ATK_MW1278D_Init();

    /* 系统启动（创建 start_task，由 start_task 创建 mqtt_task 等） */
    FreeOS_app();

    /* 不会到达这里 */
    while (1) { delay_ms(1000); }
}


