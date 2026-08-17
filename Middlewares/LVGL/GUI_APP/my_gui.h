#ifndef  _MY_GUI_H
#define _MY_GUI_H

#include <stdint.h>

/* 风扇控制全局变量 - 外部可读取 */
extern volatile uint8_t fan_running;
extern volatile uint8_t fan_speed;

/* 主入口（创建一级页面） */
void my_gui(void);

/* 读取 30 分钟 / 60 分钟 / 12 小时 / 24 小时的平均值（按当前缓冲里的数据计算） */
void my_gui_get_average(uint8_t channel, float *avg_30min, float *avg_60min,
                        float *avg_12h, float *avg_24h);

/* 给 GUI 任务周期性调用：检查 LV_RXDataQueue，
   - 有均值就刷新当前值 + 推进历史
   - 没有就什么都不做（不阻塞）
   （建议放在 my_gui_task 里 lv_timer_handler 之后调用） */
void my_gui_pump_rx_queue(void);

#endif
