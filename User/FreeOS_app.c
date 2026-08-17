/**
 ****************************************************************************************************
 * @file        FreeOS_app.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       FreeRTOS 应用入口
 *
 * 职责：
 *  - 初始化 LVGL
 *  - 创建 start_task（start_task 负责所有队列/信号量/业务任务的创建）
 *  - 启动调度器
 *
 * 所有业务任务已拆分到 Middlewares/FreeRTOS/Task/ 目录各模块
 ****************************************************************************************************
 */

/*============================================ 头文件 ============================================*/
#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"

#include "FreeOS_app.h"
#include "FreeRTOS/Task/task_start/task_start.h"

/*============================================ 外部SRAM基地址定义 ============================================*/
/* 外部SRAM地址范围: 0x68000000 ~ 0x6BFFFFFF (FSMC Bank1 NE3)
 * malloc.c 使用 mem3base[MEM3_MAX_SIZE] 从 0x68000000 开始占用 50KB
 * mem3mapbase 从 0x6800C800 开始
 * FreeRTOS堆栈/堆放在 0x68010000 之后，避免与malloc冲突
 */
#define SRAMEX_BASE   0x68010000

/*============================================ 内存分配 ============================================*/

/* 1. FreeRTOS堆 - 外部SRAM（仅在 configAPPLICATION_ALLOCATED_HEAP==1 时定义） */
#if (configAPPLICATION_ALLOCATED_HEAP == 1)
    __attribute__((at(SRAMEX_BASE))) uint8_t ucHeap[configTOTAL_HEAP_SIZE];
#endif

/* 2. 空闲任务（Idle Task）- 外部SRAM（紧跟在堆后面） */
#if (configAPPLICATION_ALLOCATED_HEAP == 1)
    static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE] 
        __attribute__((at(SRAMEX_BASE + configTOTAL_HEAP_SIZE)));
    static StaticTask_t idle_task_tcb 
        __attribute__((at(SRAMEX_BASE + configTOTAL_HEAP_SIZE + sizeof(idle_task_stack))));
#else
    static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];
    static StaticTask_t idle_task_tcb;
#endif

/* 3. 定时器任务（Timer Task）- 外部SRAM（紧跟在空闲任务后面） */
#if (configUSE_TIMERS == 1)
    #if (configAPPLICATION_ALLOCATED_HEAP == 1)
        static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH] 
            __attribute__((at(SRAMEX_BASE + configTOTAL_HEAP_SIZE + sizeof(idle_task_stack) + sizeof(idle_task_tcb))));
        static StaticTask_t timer_task_tcb 
            __attribute__((at(SRAMEX_BASE + configTOTAL_HEAP_SIZE + sizeof(idle_task_stack) + sizeof(idle_task_tcb) + sizeof(timer_task_stack))));
    #else
        static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];
        static StaticTask_t timer_task_tcb;
    #endif
#endif

/* 4. GUI任务 - 内部SRAM（保证实时性） */
StackType_t lvgl_stack[MY_GUI_STK_SIZE] __attribute__((section(".bss")));
StaticTask_t lvgl_tcb;

/*============================================ 内存回调 ============================================*/

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

#if ( configUSE_TIMERS == 1 )
    void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                         StackType_t **ppxTimerTaskStackBuffer,
                                         uint32_t *pulTimerTaskStackSize )
    {
        *ppxTimerTaskTCBBuffer = &timer_task_tcb;
        *ppxTimerTaskStackBuffer = timer_task_stack;
        *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
    }
#endif

/*============================================ FreeRTOS 入口 ============================================*/

/**
 * @brief  FreeRTOS 应用入口
 * @param  无
 * @retval 无
 */
void FreeOS_app(void)
{
    lv_init();                           /* LVGL 系统初始化 */
    lv_port_disp_init();                 /* LVGL 显示接口初始化（放在 lv_init 之后） */
    lv_port_indev_init();                /* LVGL 输入接口初始化（放在 lv_init 之后） */

    /* 创建启动任务（启动任务负责创建所有队列和业务任务） */
    xTaskCreate((TaskFunction_t )start_task,
                (const char*    )"start_task",
                (uint16_t       )START_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )START_TASK_PRIO,
                (TaskHandle_t*  )&StartTask_Handler);

    vTaskStartScheduler();               /* 开启任务调度 */
}
