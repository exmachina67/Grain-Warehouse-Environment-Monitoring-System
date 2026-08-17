#ifndef __LIGHT_H
#define __LIGHT_H

#include <stdint.h>

/******************************************************************************************************/
/* GPIO 引脚定义 */
#define fan_light       GPIO_PIN_6      /* 风扇指示灯引脚 */
#define hot_light       GPIO_PIN_7      /* 加热指示灯引脚 */
#define water_light     GPIO_PIN_8      /* 加湿指示灯引脚 */

/******************************************************************************************************/
/* 指示灯通道 ID（强类型枚举，传错类型编译器会警告） */
typedef enum {
    LIGHT_NONE = 0,        /* 不点亮任何灯 */
    LIGHT_FAN,        /* 风扇指示灯 */
    LIGHT_HEAT,        /* 加热指示灯 */
    LIGHT_HUMID       /* 加湿指示灯 */
} light_id_t;


/******************************************************************************************************/
/* 函数声明 */
void light_init(void);
void light_on(light_id_t id);   /* 点亮指定通道，其余通道熄灭 */
void light_off(light_id_t id);           /* 熄灭全部通道 */

#endif
