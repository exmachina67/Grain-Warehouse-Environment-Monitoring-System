/**
 ****************************************************************************************************
 * @file        task_gui.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       GUI 任务 — LVGL 界面显示与数据泵送
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_gui/task_gui.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"
#include "LVGL/GUI_APP/my_gui.h"

TaskHandle_t MY_GUI_Handler;

/**
 * @brief  LVGL 任务函数
 * @param  pvParameters: 传入参数(未用到)
 * @retval 无
 */
void my_gui_task(void *pvParameters)
{
    pvParameters = pvParameters;

    my_gui();   /* 画面显示 */

    while (1)
    {
        lv_timer_handler();
        my_gui_pump_rx_queue();   /* 检查 OS → GUI 数据队列（非阻塞） */
        vTaskDelay(5);
    }
}
