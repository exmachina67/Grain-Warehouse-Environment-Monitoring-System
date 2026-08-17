#ifndef __APPLAY_H
#define __APPLAY_H

#include "./SYSTEM/SYS/sys.h"
#include <stdint.h>

/******************************************************************************************************/
/* 枚举类型 */

/* 控制来源 */
typedef enum {
    SOURCE_AUTO    = 0,       /* 0-自动（传感器触发） */
    SOURCE_KEY     = 1,       /* 1-按键控制 */
    SOURCE_LORA    = 2,       /* 2-LoRa 远程控制 */
    SOURCE_GUI     = 3,       /* 3-GUI 界面控制 */
    SOURCE_NETWORK = 4,       /* 4-网络控制 */
} source_t;

/* 控制目标：哪一路 */
typedef enum {
    APPLAY_TARGET_FAN   = 0,  /* 风扇 */
    APPLAY_TARGET_HEAT  = 1,  /* 加热 */
    APPLAY_TARGET_HUMID = 2,  /* 加湿 */
} applay_target_t;

/* 控制值（语义按 target + source 解释）：
 *  - FAN:
 *      SOURCE_GUI   → value = 0~100（占空比百分数）
 *      其它来源       → value = APPLAY_VAL_OFF/LOW/MID/HI（档位枚举）
 *  - HEAT:  value = 0 关闭 / 非零 开启
 *  - HUMID: value = 0 关闭 / 非零 开启
 */
typedef enum {
    APPLAY_VAL_OFF = 0,       /* 通用关闭 / 0% 占空比 */
    APPLAY_VAL_ON  = 1,       /* HEAT/HUMID：开启 */
    APPLAY_VAL_LOW = 1,       /* FAN：低速档 */
    APPLAY_VAL_MID = 2,       /* FAN：中速档 */
    APPLAY_VAL_HI  = 3,       /* FAN：高速档 */
} applay_val_t;

/* LED 模式（保留类型，方便业务层使用） */
typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_FAN,
    LED_HEAT,
    LED_HUMID,
    LED_LIGHT
} led_mode_t;

/******************************************************************************************************/
/* 【统一结构体】所有控制信号都用这个
 *
 *  - 不管来自 GUI / sensor / lora / key，都构造一份 applay_msg_t 投到 ActuatorCmdQueue
 *  - applay_task 消费，按 target 调对应的 *_apply()
 *  - *_apply() 内部按 source 决定如何解释 value（档位 / 占空比 / 开关） */
typedef struct {
    applay_target_t target;   /* 目标通道 */
    applay_val_t    value;    /* 控制值（语义由 target + source 决定） */
    source_t        source;   /* 来源 */
} applay_msg_t;

/******************************************************************************************************/

extern volatile uint8_t mode_control;

/* 各路执行器私有动作：只控制自家硬件，外部统一经 applay_dispatch() 转发 */
void fan_apply  (const applay_msg_t *msg);
void heat_apply (const applay_msg_t *msg);
void humid_apply(const applay_msg_t *msg);

/* 同步分派入口：保留供启动期 / 紧急停止等必须同步执行的场景使用。
 * 正常运行所有控制信号都投到 ActuatorCmdQueue，由 applay_task 异步消费。 */
void applay_dispatch(const applay_msg_t *msg);

#endif



