/**
 ****************************************************************************************************
 * @file        applay.c
 * @brief       应用层：风扇 / 加热 / 加湿 三路独立控制
 *              - fan_apply()  只管风扇（电机 + 风扇指示灯 + 风扇状态）
 *              - heat_apply() 只管加热（加热器 + 加热指示灯）
 *              - humid_apply()只管加湿（加湿器 + 加湿指示灯）
 *
 *  所有 *_apply() 都接收统一的 applay_msg_t，按 msg->source 决定 msg->value 的语义：
 *    - SOURCE_GUI          → value 当作 0~100% 占空比（PWM = pct * 3）
 *    - 其它来源（SENSOR/LORA/KEY/NETWORK）→ value 当作档位/开关枚举
 ****************************************************************************************************
 */

#include "./BSP/APPLAY/applay.h"
#include "./BSP/motor/gtim.h"
#include "./BSP/LIGHT/light.h"
#include "./BSP/ST021/st021.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/******************************************************************************************************/
/* 外部 OS 资源（FreeOS_app.c 中定义） */
extern SemaphoreHandle_t FanMutex;

/******************************************************************************************************/
/* 外部全局状态（FreeOS_app.c / 其它模块提供） */
extern volatile uint8_t fan_running;
extern volatile uint8_t fan_speed;
volatile uint8_t mode_control;
extern volatile uint8_t heat_running;
extern volatile uint8_t humid_running;

/******************************************************************************************************/
/* PWM 速度档位（PWM 比较值范围 0~300，由 gtim.c 约束） */
#define MOTOR_PWM_LOW      100
#define MOTOR_PWM_MID      200
#define MOTOR_PWM_HIGH     300
#define MOTOR_PWM_OFF      0

/* 风扇速度百分比显示值 */
#define FAN_PCT_LOW        33
#define FAN_PCT_MID        66
#define FAN_PCT_HIGH       100

/******************************************************************************************************/
/**
 * @brief   风扇控制（只控风扇）
 * @param   msg : 风扇消息（target/value/source）
 * @retval  无
 *
 * 范围：
 *  - 电机 PWM
 *  - 风扇指示灯（light.h 中 LIGHT_FAN）
 *  - 全局状态 fan_running / fan_speed / mode_control
 *  - 不动加热、不动加湿
 *
 * value 语义：
 *  - SOURCE_GUI   → 0~100 的占空比百分数（PWM = pct * 3）
 *  - 其它来源       → APPLAY_VAL_OFF/LOW/MID/HI（档位枚举）
 */
void fan_apply(const applay_msg_t *msg)
{
    if (msg == NULL) return;

    /* 取互斥锁，防止与 sensor_task / lora_task / key_task 等并发写状态 */
    if (FanMutex != NULL) {
        xSemaphoreTake(FanMutex, portMAX_DELAY);
    }

    /* ========== GUI 专用通路：按 0~100% 占空比直接写 PWM ==========
     * 约定：GUI 源把 value 当作 0~100 的占空比百分数直接发过来；
     *  PWM 范围 0~300，PWM = pct * 3。
     *  GUI 视作手动接管：把 mode_control 置 1。 */
    if (msg->source == SOURCE_GUI)
    {
        uint8_t pct = ((uint8_t)msg->value <= 100) ? (uint8_t)msg->value : 100;
        int     pwm = (int)((uint32_t)pct * 3U);   /* 0~300 */

        if (pct == 0) {
            motor_set_speed(0);
            fan_running = 0;
            fan_speed   = 0;
            light_off(LIGHT_FAN);
        } else {
            motor_set_speed(pwm);
            fan_running = 1;
            fan_speed   = pct;
            light_on(LIGHT_FAN);
        }
        mode_control = 1;       /* GUI 改动一律视作手动接管 */

        if (FanMutex != NULL) {
            xSemaphoreGive(FanMutex);
        }
        return;
    }

    /* ========== 其它来源（sensor / lora / key）：按档位映射 ==========
     *  - value 0 = OFF；1=LOW；2=MID；3=HIGH
     *  - 自动（sensor）不下 mode_control；手动（lora/key）下 mode_control=1 抢回手动权 */
    uint8_t is_manual = (msg->source != SOURCE_AUTO);

    switch (msg->value)
    {
        /* ----------------------------- 关闭 ----------------------------- */
        case APPLAY_VAL_OFF:
            motor_set_speed(MOTOR_PWM_OFF);
            fan_running = 0;
            fan_speed   = 0;
            if (is_manual) mode_control = 1;

            light_off(LIGHT_FAN);
            break;

        /* ----------------------------- 低速 ----------------------------- */
        case APPLAY_VAL_LOW:
            motor_set_speed(MOTOR_PWM_LOW);
            fan_running = 1;
            fan_speed   = FAN_PCT_LOW;
            if (is_manual) mode_control = 1;

            light_on(LIGHT_FAN);
            break;

        /* ----------------------------- 中速 ----------------------------- */
        case APPLAY_VAL_MID:
            motor_set_speed(MOTOR_PWM_MID);
            fan_running = 1;
            fan_speed   = FAN_PCT_MID;
            if (is_manual) mode_control = 1;

            light_on(LIGHT_FAN);
            break;

        /* ----------------------------- 高速 ----------------------------- */
        case APPLAY_VAL_HI:
            motor_set_speed(MOTOR_PWM_HIGH);
            fan_running = 1;
            fan_speed   = FAN_PCT_HIGH;
            if (is_manual) mode_control = 1;

            light_on(LIGHT_FAN);
            break;

        /* ----------------------------- 默认 ----------------------------- */
        default:
            break;
    }

    if (FanMutex != NULL) {
        xSemaphoreGive(FanMutex);
    }
}

/******************************************************************************************************/
/**
 * @brief   加热控制（独立通道）
 * @param   msg : 加热消息（target/value/source）
 * @retval  无
 *
 * 范围：
 *  - ST021 加热器
 *  - 加热指示灯（light.h 中 LIGHT_HEAT）
 *  - 全局状态 heat_running
 *
 * value 语义：0=关 / 非零=开；source 仅作未来审计扩展，不影响本通道的开关语义。
 */
void heat_apply(const applay_msg_t *msg)
{
    if (msg == NULL) return;

    if (FanMutex != NULL) {
        xSemaphoreTake(FanMutex, portMAX_DELAY);
    }

    if (msg->value == APPLAY_VAL_OFF) {
        ST021_CON(0);                  /* 关加热 */
        heat_running = 0;
        light_off(LIGHT_HEAT);
    } else {
        ST021_CON(1);                  /* 开加热 */
        heat_running = 1;
        light_on(LIGHT_HEAT);
    }

    if (FanMutex != NULL) {
        xSemaphoreGive(FanMutex);
    }
}

/******************************************************************************************************/
/**
 * @brief   加湿控制（独立通道）
 * @param   msg : 加湿消息（target/value/source）
 * @retval  无
 *
 * 范围：
 *  - 加湿器（当前无专用 BSP，留 TODO 占位）
 *  - 加湿指示灯（light.h 中 LIGHT_HUMID）
 *  - 全局状态 humid_running
 *
 * value 语义：0=关 / 非零=开；source 仅作未来审计扩展。
 */
void humid_apply(const applay_msg_t *msg)
{
    if (msg == NULL) return;

    if (FanMutex != NULL) {
        xSemaphoreTake(FanMutex, portMAX_DELAY);
    }

    if (msg->value == APPLAY_VAL_OFF) {
        /* TODO: 关闭加湿硬件（如雾化片/继电器） */
        humid_running = 0;
        light_off(LIGHT_HUMID);
    } else {
        /* TODO: 打开加湿硬件 */
        humid_running = 1;
        light_on(LIGHT_HUMID);
    }

    if (FanMutex != NULL) {
        xSemaphoreGive(FanMutex);
    }
}

/******************************************************************************************************/
/**
 * @brief   统一分派入口（保留供同步调用场景，如启动期、紧急停止）
 * @param   msg : applay_msg_t (target + value + source)
 * @retval  无
 *
 * 所有控制信号正常都投到 ActuatorCmdQueue，由 applay_task 异步消费；
 * 这个函数只给"必须同步执行"的场景（如启动期、紧急停止）使用。
 * 内部直接调 fan/heat/humid_apply，不再构造中间结构体。
 */
void applay_dispatch(const applay_msg_t *msg)
{
    if (msg == NULL) return;

    switch (msg->target)
    {
        case APPLAY_TARGET_FAN:   fan_apply(msg);   break;
        case APPLAY_TARGET_HEAT:  heat_apply(msg);  break;
        case APPLAY_TARGET_HUMID: humid_apply(msg); break;
        default: break;
    }
}
