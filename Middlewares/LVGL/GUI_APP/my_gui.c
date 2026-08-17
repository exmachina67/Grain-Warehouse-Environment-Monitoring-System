#include "LVGL/GUI_APP/my_gui.h"
#include "lvgl/lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "portable.h"
#include "stm32f4xx_hal.h"
#include "FreeOS_app.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 * 历史数据环形缓冲（每个通道独立，最多保留 1 小时）
 * 每分钟 1 个采样点，60 槽位 = 1 小时数据
 * 实际工程中应替换为 RTC + EEPROM/Flash 持久化方案
 * ============================================================ */
#define HISTORY_BUCKETS  60   /* 60 个槽位，1 个/分钟，保留 1 小时 */
#define HOUR_HISTORY       24

/* 通道索引 */
enum {
    CH_TEMP = 0,
    CH_HUM  = 1,
    CH_CO2  = 2,
    CH_CO   = 3,
    CH_MAX
};

/* 每通道环形缓冲（24 个 float，CO2/CO 用 float 便于算平均） */
static float history[CH_MAX][HISTORY_BUCKETS];
static uint8_t hist_count[CH_MAX];     /* 已写入数量（≤ HISTORY_BUCKETS） */
static uint8_t hist_head[CH_MAX];      /* 下一个写入位置 */

static float history_hour[CH_MAX][HOUR_HISTORY];    /* 24h 存储记录 */
static uint8_t hour_count[CH_MAX];
static uint8_t hour_head[CH_MAX];

/* 当前实时值（从外部喂入） */
/* 当前实时值（按通道索引；下标 == CH_*，与 chan_info[] 一一对应） */
static float cur_values[CH_MAX];

static inline void chan_set(uint8_t ch, float v) { cur_values[ch] = v; }
static inline float chan_get(uint8_t ch)        { return cur_values[ch]; }
static uint8_t has_current_data = 0;  /* 上电未喂数据时显示 "--" */

/* 通道元信息 */
typedef struct {
    const char *name;          /* 英文显示名 */
    const char *unit;             /* 单位字符串 */
    float  min;                       /* Y 轴下限（图标用） */
    float  max;                       /* Y 轴上限 */
    uint32_t    color;              /* 主色（hex 数字，0xRRGGBB），运行时转 lv_color_t */
    const lv_font_t *big_font; /* 当前值字体 */
} chan_info_t;

static const chan_info_t chan_info[CH_MAX] = {
    { "Temperature", "C",   -10.0f,  60.0f, 0xFFA500, &lv_font_montserrat_24 },
    { "Humidity",    "%",     0.0f, 100.0f, 0x00BFFF, &lv_font_montserrat_24 },
    { "CO2",         "ppm",   0.0f, 5000.0f, 0xAA00AA, &lv_font_montserrat_24 },
    { "CO",          "ppm",   0.0f,  500.0f, 0xCC0000, &lv_font_montserrat_24 },
};

/* ============================================================
 * 一级页面（主页）控件
 * ============================================================ */
static lv_obj_t *temp_label;
static lv_obj_t *humidity_label;
static lv_obj_t *co2_label;
static lv_obj_t *co_label;
static lv_obj_t *home_label_title;
static lv_obj_t *fan_switch;
static lv_obj_t *fan_slider;
static lv_obj_t *fan_speed_label;
static lv_obj_t *alert_led;
static lv_obj_t *alert_label_lbl;     /* alert_led 里的文字 "OK" / "ALERT!" */
static lv_obj_t *status_label;        /* "System Status: Normal/Warning" 文本 */
static lv_obj_t *temp_chart;
static lv_chart_series_t *temp_ser;
static lv_chart_series_t *humidity_ser;
static lv_obj_t *chart_container;
static lv_obj_t *channel_buttons_bar;
static lv_obj_t *chart_title;
static lv_obj_t *home_root;          /* 一级页面上的所有可见对象在它之下 */
static lv_obj_t *detail_root;        /* 二级页面（默认隐藏） */
static lv_obj_t *sys_root;           /* 系统占用页（默认隐藏，与 detail_root 平级） */
static uint8_t detail_channel = 0;  /* 当前二级页面对应的通道 */
static uint8_t chart_fullscreen = 0; // 全屏状态标志（0=正常，1=全屏）
static uint8_t sensor_data_count_min = 0;

/* 图表全屏时使用的临时对象 / 备份 */
static lv_obj_t *fs_back_btn = NULL;          /* 全屏 BACK 按钮，避免重复创建 */
static char fs_title_backup[64];              /* 全屏前 home 标题原文，用于退出时还原 */


/* 风扇控制全局变量 - 外部可访问 */
volatile uint8_t fan_running = 0;
volatile uint8_t fan_speed = 50;
volatile uint8_t heat_running = 0;
volatile uint8_t humid_running = 0;

/* 报警自动联动：1=当前是报警自动开启的风扇，0=用户手动开关或未开
 * 用来在报警解除时只关闭"自动开的"，保留"手动开的" */
static uint8_t alert_auto_on = 0;

/* ============================================================
 * 函数前向声明
 * ============================================================ */
static void fan_switch_event_cb(lv_event_t * e);
static void fan_slider_event_cb(lv_event_t * e);
static void create_sensor_section(lv_obj_t * parent);
static void create_fan_control_section(lv_obj_t *parent);
static void create_alert_section(lv_obj_t *parent);
static void create_chart_section(lv_obj_t *parent);
static void chart_fullscreen_event_cb(lv_event_t *e);
static void back_butten_event_cb(lv_event_t *e);
static void create_detail_page(lv_obj_t *parent);
static void detail_back_event_cb(lv_event_t * e);
static void detail_channel_btn_event_cb(lv_event_t * e);
static void create_system_page(lv_obj_t *parent);
static void sys_back_event_cb(lv_event_t * e);
static void sys_btn_event_cb(lv_event_t * e);
static void update_system_page(void);

/* 递归遍历 obj 的所有后代，对每个 obj 设置/清除 flag（HIDDEN 等）
 * lvgl v8 的 lv_obj_add_flag 不会递归到子对象，必须自己写 */
static void set_flag_recursive(lv_obj_t *obj, uint32_t flag, bool set)
{
    if (!obj) return;
    if (set) lv_obj_add_flag(obj, flag);
    else     lv_obj_clear_flag(obj, flag);

    uint32_t cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        set_flag_recursive(lv_obj_get_child(obj, i), flag, set);
    }
}
static void update_sensor_display(void);
static void update_chart_data(void);
static void update_detail_page(uint8_t ch);
static void history_push_one(uint8_t ch, float v);
static void history_push_hour(uint8_t ch, float v);

/* ============================================================
 * 公开 API
 * ============================================================ */

/* 把 env_avg_t 中某个通道的原始字段"翻译"成 GUI 显示用的 float 值。
 * 唯一一处 OS 结构体字段 → 通道索引的映射；OS 加新字段只动这里。 */
static float chan_pick_value_from_avg(uint8_t ch, const env_avg_t *a)
{
    switch (ch) {
        case CH_TEMP: return (float)a->temp_avg_x10 / 10.0f;   /* ×10 → ℃ */
        case CH_HUM:  return (float)a->humi_avg_x10 / 10.0f;
        case CH_CO2:  return (float)a->co2_avg;
        case CH_CO:   return (float)a->co_avg;
        default:      return 0.0f;
    }
}

/* 把一份 env_avg_t 应用到 GUI（更新当前值 + 推进历史 + 刷新显示）
 * 用 chan_info[] 表驱动，扩展通道无需改本函数。 */
static void apply_avg_to_gui(const env_avg_t *avg)
{
    /* 把均值按通道写入 cur_values（按表遍历） */
    for (uint8_t ch = 0; ch < CH_MAX; ch++) {
        chan_set(ch, chan_pick_value_from_avg(ch, avg));
    }
    has_current_data = 1;

    /*  整点：把上一小时的 60min 平均写入 hour 缓冲（每个通道） */
    if (sensor_data_count_min >= 60) {
        sensor_data_count_min = 0;
        for (uint8_t ch = 0; ch < CH_MAX; ch++) {
            uint8_t n = hist_count[ch];
            if (n == 0) continue;
            float sum = 0.0f;
            for (uint8_t i = 0; i < n; i++) sum += history[ch][i];
            history_push_hour(ch, sum / n);
        }
    }
    sensor_data_count_min++;

    /* 3) 每个通道推一个历史采样点（min 粒度） */
    for (uint8_t ch = 0; ch < CH_MAX; ch++) {
        history_push_one(ch, chan_get(ch));
    }

    update_sensor_display();
    update_chart_data();
    /* 二级页面打开时也要刷新 */
    if (detail_root && lv_obj_is_visible(detail_root)) {
        update_detail_page(detail_channel);
    }
    /* 系统占用页打开时也刷新 */
    if (sys_root && lv_obj_is_visible(sys_root)) {
        update_system_page();
    }
}

/* 从 OS 队列里取最新均值（不阻塞），有就应用（GUI 任务周期性调用） */
void my_gui_pump_rx_queue(void)
{
    if (LV_RXDataQueue == NULL) return;

    env_avg_t avg;
    if (xQueueReceive(LV_RXDataQueue, &avg, 0) == pdPASS) {
        apply_avg_to_gui(&avg);
    }
}

/* 读取 30 / 60 分钟平均 + 12 / 24 小时平均。
 * 全部按环形缓冲从最新点反向取 take 个；指针为 NULL 表示不取该值。 */
void my_gui_get_average(uint8_t channel, float *avg_30min, float *avg_60min,
                        float *avg_12h, float *avg_24h)
{
    /* 没数据 / 通道越界：默认返回 -1.0f，调用方据此显示 "--" */
    if (avg_30min) *avg_30min = -1.0f;
    if (avg_60min) *avg_60min = -1.0f;
    if (avg_12h)   *avg_12h   = -1.0f;
    if (avg_24h)   *avg_24h   = -1.0f;

    if (channel >= CH_MAX) return;

    uint8_t n  = hist_count[channel];
    uint8_t nh = hour_count[channel];

    /* ---- 分钟级平均 ---- */
    if (n > 0) {
        /* 60min 平均 = 缓冲内全部样本（最多 60 个） */
        if (avg_60min) {
            float sum = 0.0f;
            for (uint8_t i = 0; i < n; i++) sum += history[channel][i];
            *avg_60min = sum / n;
        }
        /* 30min 平均 = 最新 30 个（按环形缓冲反序取） */
        if (avg_30min) {
            uint8_t take = (n >= 30) ? 30 : n;
            float sum = 0.0f;
            for (uint8_t i = 0; i < take; i++) {
                int8_t idx = (int8_t)hist_head[channel] - 1 - i;
                if (idx < 0) idx += HISTORY_BUCKETS;
                sum += history[channel][idx];
            }
            *avg_30min = sum / take;
        }
    }

    /* ---- 小时级平均（仅当 hour 缓冲里有数据时算） ---- */
    if (nh > 0) {
        /* 24h 平均 = 最新 24 个（环形反序） */
        if (avg_24h) {
            uint8_t take = (nh >= HOUR_HISTORY) ? HOUR_HISTORY : nh;
            float sum = 0.0f;
            for (uint8_t i = 0; i < take; i++) {
                int8_t idx = (int8_t)hour_head[channel] - 1 - i;
                if (idx < 0) idx += HOUR_HISTORY;
                sum += history_hour[channel][idx];
            }
            *avg_24h = sum / take;
        }
        /* 12h 平均 = 最新 12 个（环形反序） */
        if (avg_12h) {
            uint8_t take = (nh >= 12) ? 12 : nh;
            float sum = 0.0f;
            for (uint8_t i = 0; i < take; i++) {
                int8_t idx = (int8_t)hour_head[channel] - 1 - i;
                if (idx < 0) idx += HOUR_HISTORY;
                sum += history_hour[channel][idx];
            }
            *avg_12h = sum / take;
        }
    }
}

/* ============================================================
 * 内部：环形缓冲写入 每分
 * ============================================================ */
static void history_push_one(uint8_t ch, float v)
{
    history[ch][hist_head[ch]] = v;
    hist_head[ch] = (hist_head[ch] + 1) % HISTORY_BUCKETS;
    if (hist_count[ch] < HISTORY_BUCKETS) hist_count[ch]++;
}

/* ============================================================
 * 内部：环形缓冲写入 每时
 * ============================================================ */
static void history_push_hour(uint8_t ch, float v)
{
    history_hour[ch][hour_head[ch]] = v;
    hour_head[ch] = (hour_head[ch] + 1) % HOUR_HISTORY;
    if (hour_count[ch] < HOUR_HISTORY) hour_count[ch]++;
}

/* ============================================================
 * 内部：历史缓冲初始化（不做任何填充，没有就是没有）
 * ============================================================ */
static void history_init(void)
{
    for (uint8_t ch = 0; ch < CH_MAX; ch++) {
        hist_head[ch]   = 0;
        hist_count[ch]  = 0;
        hour_head[ch]   = 0;
        hour_count[ch]  = 0;

        for (uint8_t i = 0; i < HISTORY_BUCKETS; i++) history[ch][i] = 0.0f;
        for (uint8_t i = 0; i < HOUR_HISTORY;  i++) history_hour[ch][i] = 0.0f;
    }
    sensor_data_count_min = 0;
}

/* ============================================================
 * 更新显示数据
 * ============================================================ */
static void update_sensor_display(void)
{
    char buf[32];

    if (has_current_data) {
        sprintf(buf, "%d C", (int)chan_get(CH_TEMP));
        lv_label_set_text(temp_label, buf);

        sprintf(buf, "%d %%", (int)chan_get(CH_HUM));
        lv_label_set_text(humidity_label, buf);

        sprintf(buf, "%d ppm", (int)chan_get(CH_CO2));
        lv_label_set_text(co2_label, buf);

        sprintf(buf, "%d ppm", (int)chan_get(CH_CO));
        lv_label_set_text(co_label, buf);
    } else {
        lv_label_set_text(temp_label, "-- C");
        lv_label_set_text(humidity_label, "-- %");
        lv_label_set_text(co2_label, "-- ppm");
        lv_label_set_text(co_label, "-- ppm");
    }

    /* 报警 LED 简单策略：CO2>1500 或 CO>100 → ALERT */
    uint8_t alert_now = has_current_data &&
                        ((chan_get(CH_CO2) > 1500.0f) || (chan_get(CH_CO) > 100.0f));
    if (alert_now) {
        lv_obj_set_style_bg_color(alert_led, lv_color_hex(0xFF0000), 0);
        if (alert_label_lbl) lv_label_set_text(alert_label_lbl, "ALERT!");
        if (status_label) {
            lv_label_set_text(status_label, "System Status: WARNING");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        }

        /* 报警联动：自动开风扇（只发一次：避免每分钟重复发队列）
         * 用户手动开过的也会被统一刷一次状态（安全），但不会清掉用户意图 */
        if (!alert_auto_on) {
            alert_auto_on = 1;
            fan_running = 1;

            /* UI：开关 ON、滑块解禁、默认推到 80%（如果当前更低就抬高） */
            if (fan_switch) lv_obj_add_state(fan_switch, LV_STATE_CHECKED);
            if (fan_slider) {
                lv_obj_clear_state(fan_slider, LV_STATE_DISABLED);
                if ((uint8_t)lv_slider_get_value(fan_slider) < 80) {
                    lv_slider_set_value(fan_slider, 80, LV_ANIM_ON);
                    fan_speed = 80;
                    if (fan_speed_label) lv_label_set_text(fan_speed_label, "Speed: 80%");
                } else {
                    fan_speed = (uint8_t)lv_slider_get_value(fan_slider);
                }
            }

            /* 发一条执行器命令（80% PWM） */
            applay_msg_t am = {
                .target = APPLAY_TARGET_FAN,
                .source = SOURCE_GUI,
                .value  = (applay_val_t)fan_speed,
            };
            if (ActuatorCmdQueue != NULL) {
                (void)xQueueSend(ActuatorCmdQueue, &am, 0);
            }
        }
    } else {
        lv_obj_set_style_bg_color(alert_led, lv_color_hex(0x00FF00), 0);
        if (alert_label_lbl) lv_label_set_text(alert_label_lbl, "OK");
        if (status_label) {
            lv_label_set_text(status_label, "System Status: Normal");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x000000), 0);
        }

        /* 报警解除：如果是报警自动开的，关掉；用户手动开的，保留 */
        if (alert_auto_on) {
            alert_auto_on = 0;
            fan_running = 0;

            if (fan_switch) lv_obj_clear_state(fan_switch, LV_STATE_CHECKED);
            if (fan_slider) {
                lv_obj_add_state(fan_slider, LV_STATE_DISABLED);
                lv_slider_set_value(fan_slider, 0, LV_ANIM_ON);
                fan_speed = 0;
                if (fan_speed_label) lv_label_set_text(fan_speed_label, "Speed: 0%");
            }

            applay_msg_t am = {
                .target = APPLAY_TARGET_FAN,
                .source = SOURCE_GUI,
                .value  = APPLAY_VAL_OFF,
            };
            if (ActuatorCmdQueue != NULL) {
                (void)xQueueSend(ActuatorCmdQueue, &am, 0);
            }
        }
    }
}

static void update_chart_data(void)
{
    /* 未喂过实时数据：图表保持空白，避免把初始值 25/60 当成第一个数据点 */
    if (!has_current_data) return;
    lv_chart_set_next_value(temp_chart, temp_ser, (lv_coord_t)chan_get(CH_TEMP));
    lv_chart_set_next_value(temp_chart, humidity_ser, (lv_coord_t)chan_get(CH_HUM));
}

/* ============================================================
 * 风扇回调（沿用旧逻辑）
 * ============================================================ */
static void fan_switch_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    applay_msg_t am = {
        .target = APPLAY_TARGET_FAN,
        .source = SOURCE_GUI,
    };

    if(lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        fan_running = 1;
        /* 用户主动打开开关 → 接管控制权，清掉"自动开"标记 */
        alert_auto_on = 0;
        /* 开关打开：取消禁用，沿用当前滑块值（PWM=滑块当前值） */
        lv_obj_clear_state(fan_slider, LV_STATE_DISABLED);
        am.value = (applay_val_t)lv_slider_get_value(fan_slider);
    } else {
        fan_running = 0;
        /* 用户主动关 → 也不再是"自动开的" */
        alert_auto_on = 0;
        /* 开关关闭：发 0%（PWM=0），电机停转 */
        am.value = APPLAY_VAL_OFF;
        /* 滑块禁用 + 动画滑到 0，给用户一个"关闭"的视觉反馈 */
        lv_obj_add_state(fan_slider, LV_STATE_DISABLED);
        lv_slider_set_value(fan_slider, 0, LV_ANIM_ON);
        /* disabled 状态下 LVGL 不触发 VALUE_CHANGED 事件，
         * 所以这里手动同步 label 和 fan_speed 变量 */
        fan_speed = 0;
        lv_label_set_text(fan_speed_label, "Speed: 0%");
    }

    if (ActuatorCmdQueue != NULL) {
        (void)xQueueSend(ActuatorCmdQueue, &am, 0);
    }
}

static void fan_slider_event_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    int32_t pct = lv_slider_get_value(slider);  /* 0~100 占空比百分数 */
    fan_speed = (uint8_t)pct;

    char buf[32];
    sprintf(buf, "Speed: %d%%", fan_speed);
    lv_label_set_text(fan_speed_label, buf);

    applay_msg_t am = {
        .target = APPLAY_TARGET_FAN,
        .value  = (applay_val_t)pct,
        .source = SOURCE_GUI,
    };
    if (ActuatorCmdQueue != NULL) {
        (void)xQueueSend(ActuatorCmdQueue, &am, 0);
    }
}

/* ============================================================
 * 进入图表全屏回调（沿用旧逻辑）
 * ============================================================ */
 static void chart_fullscreen_event_cb(lv_event_t *e)
 {
     lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);

    /* ========== 单击图表进入全屏 ========== */
    if(code != LV_EVENT_CLICKED) return;
    if(target != chart_container && target != temp_chart) return;
    if(chart_fullscreen) return;  // 已经全屏则忽略

    chart_fullscreen = 1;
   // 放大容器（全屏)
        lv_label_set_text(chart_title, "");
        lv_obj_set_size(chart_container, 790, 410);
        lv_obj_align(chart_container, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_size(temp_chart, 760, 380);
        lv_obj_align(temp_chart, LV_ALIGN_CENTER, 0, 0);
        lv_chart_set_point_count(temp_chart, 100);  // 显示更多数据点
        lv_chart_set_div_line_count(temp_chart, 10, 20);

    // 更改标题名（先备份原文，退出全屏时还原）
    if (home_label_title != NULL)
    {
        const char *cur = lv_label_get_text(home_label_title);
        if (cur) {
            strncpy(fs_title_backup, cur, sizeof(fs_title_backup) - 1);
            fs_title_backup[sizeof(fs_title_backup) - 1] = '\0';
        } else {
            fs_title_backup[0] = '\0';
        }
        lv_label_set_text(home_label_title, "Data Trends");
    }

    // 创建BACK 按钮（用静态指针记录，重复进入全屏时先清掉旧的）
    if (fs_back_btn) {
        lv_obj_del(fs_back_btn);
        fs_back_btn = NULL;
    }
    fs_back_btn = lv_obj_create(home_root);
    lv_obj_set_size(fs_back_btn, 70, 26);
    lv_obj_align(fs_back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(fs_back_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_radius(fs_back_btn, 5, 0);
    lv_obj_set_scroll_dir(fs_back_btn, LV_DIR_NONE);

    lv_obj_t *btn_label = lv_label_create(fs_back_btn);
    lv_label_set_text(btn_label, "< BACK");
    lv_obj_align(btn_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(fs_back_btn, back_butten_event_cb, LV_EVENT_CLICKED, NULL);

    // 隐藏其他所有控件
    lv_obj_add_flag(temp_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(humidity_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(co2_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(co_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(fan_switch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(fan_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(fan_speed_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(alert_led, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(channel_buttons_bar, LV_OBJ_FLAG_HIDDEN);

 }
 /* ============================================================
 * 退出图表全屏回调（沿用旧逻辑）
 * ============================================================ */
 static void back_butten_event_cb(lv_event_t *e)
 {
     (void)e;
     /* 兜底：拿不到就用静态指针 */
     lv_obj_t *btu = fs_back_btn;

     chart_fullscreen = 0;

     // 恢复表格状态
     lv_label_set_text(chart_title, "Data Trends");
     lv_obj_set_size(chart_container, 390, 310);
    lv_obj_align(chart_container, LV_ALIGN_TOP_LEFT, 380, 40);
    lv_obj_set_size(temp_chart, 370, 260);
    lv_obj_align(temp_chart, LV_ALIGN_TOP_MID, 0, 32);
    lv_chart_set_point_count(temp_chart, 20);
    lv_chart_set_div_line_count(temp_chart, 5, 10);

    // 删除退出按键（用静态指针，调用后清空）
    if (fs_back_btn) {
        lv_obj_del(fs_back_btn);
        fs_back_btn = NULL;
    }

    // 改回 home 标题：优先用 backup，没有再硬编码
    if (home_label_title != NULL) {
        if (fs_title_backup[0] != '\0') {
            lv_label_set_text(home_label_title, fs_title_backup);
        } else {
            lv_label_set_text(home_label_title, "Grain Warehouse Environment Monitoring System");
        }
    }

    // 显示其他控件
    lv_obj_clear_flag(temp_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(humidity_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(co2_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(co_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(fan_switch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(fan_speed_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(fan_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(alert_led, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(channel_buttons_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(temp_chart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(chart_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(chart_container, LV_OBJ_FLAG_HIDDEN);

    (void)btu;
 }
/* ============================================================
 * 一级页面：传感器区
 * ============================================================ */
static void create_sensor_section(lv_obj_t * parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, 380, 120);
    lv_obj_align(container, LV_ALIGN_TOP_LEFT, -15, 40);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(container, LV_DIR_NONE);

    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "Sensor Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 5);

    /* 2x2 网格：左列 10/55，右列 200/245；上排 Y=35，下排 Y=75 */
    /* 上排 左：Temp  text show  */
    lv_obj_t *temp_title = lv_label_create(container);
    lv_label_set_text(temp_title, "Temp:");
    lv_obj_set_style_text_font(temp_title, &lv_font_montserrat_12, 0);
    lv_obj_align(temp_title, LV_ALIGN_TOP_LEFT, 15, 38);

    temp_label = lv_label_create(container);
    lv_label_set_text(temp_label, "--");
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 65, 35);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFFA500), 0);

    /* 上排 右：Hum  text show*/
    lv_obj_t *hum_title = lv_label_create(container);
    lv_label_set_text(hum_title, "Hum:");
    lv_obj_set_style_text_font(hum_title, &lv_font_montserrat_12, 0);
    lv_obj_align(hum_title, LV_ALIGN_TOP_LEFT, 200, 38);

    humidity_label = lv_label_create(container);
    lv_label_set_text(humidity_label, "--");
    lv_obj_align(humidity_label, LV_ALIGN_TOP_LEFT, 245, 35);
    lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(humidity_label, lv_color_hex(0x00BFFF), 0);

    /* 下排 左：CO2  text show*/
    lv_obj_t *co2_title = lv_label_create(container);
    lv_label_set_text(co2_title, "CO2:");
    lv_obj_set_style_text_font(co2_title, &lv_font_montserrat_12, 0);
    lv_obj_align(co2_title, LV_ALIGN_TOP_LEFT, 15, 78);

    co2_label = lv_label_create(container);
    lv_label_set_text(co2_label, "--");
    lv_obj_align(co2_label, LV_ALIGN_TOP_LEFT, 65, 75);
    lv_obj_set_style_text_font(co2_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(co2_label, lv_color_hex(0x666666), 0);

    /* 下排 右：CO  text show*/
    lv_obj_t *co_title = lv_label_create(container);
    lv_label_set_text(co_title, "CO:");
    lv_obj_set_style_text_font(co_title, &lv_font_montserrat_12, 0);
    lv_obj_align(co_title, LV_ALIGN_TOP_LEFT, 200, 78);

    co_label = lv_label_create(container);
    lv_label_set_text(co_label, "--");
    lv_obj_align(co_label, LV_ALIGN_TOP_LEFT, 245, 75);
    lv_obj_set_style_text_font(co_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(co_label, lv_color_hex(0xFF0000), 0);
}

/* ============================================================
 * 一级页面：风扇控制
 * ============================================================ */
static void create_fan_control_section(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, 380, 90);
    lv_obj_align(container, LV_ALIGN_TOP_LEFT, -15, 170);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(container, LV_DIR_NONE);

    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "Fan Control");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 5);

    fan_speed_label = lv_label_create(container);
    lv_label_set_text(fan_speed_label, "Speed: 50%");
    lv_obj_align(fan_speed_label, LV_ALIGN_TOP_LEFT, 10, 28);
    lv_obj_set_style_text_font(fan_speed_label, &lv_font_montserrat_12, 0);

    fan_slider = lv_slider_create(container);
    lv_obj_set_size(fan_slider, 240, 12);
    lv_obj_align(fan_slider, LV_ALIGN_TOP_LEFT, 10, 55);
    lv_slider_set_range(fan_slider, 0, 100);
    lv_slider_set_value(fan_slider, 50, LV_ANIM_OFF);
    lv_obj_add_state(fan_slider, LV_STATE_DISABLED);
    lv_obj_add_event_cb(fan_slider, fan_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 右侧：开关 + ON/OFF 文字 */
    fan_switch = lv_switch_create(container);
    lv_obj_set_size(fan_switch, 50, 25);
    lv_obj_align(fan_switch, LV_ALIGN_TOP_RIGHT, -55, 50);
    lv_obj_add_event_cb(fan_switch, fan_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *switch_label = lv_label_create(container);
    lv_label_set_text(switch_label, "ON/OFF");
    lv_obj_align(switch_label, LV_ALIGN_TOP_RIGHT, 0, 55);
    lv_obj_set_style_text_font(switch_label, &lv_font_montserrat_12, 0);
}

/* ============================================================
 * 一级页面：报警区
 * ============================================================ */
static void create_alert_section(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, 380, 76);
    lv_obj_align(container, LV_ALIGN_TOP_LEFT, -15, 270);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(container, LV_DIR_NONE);

    /* 圆形状态灯 */
    alert_led = lv_obj_create(container);
    lv_obj_set_size(alert_led, 40, 40);
    lv_obj_align(alert_led, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_bg_color(alert_led, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_radius(alert_led, 20, 0);
    lv_obj_set_style_border_width(alert_led, 0, 0);

    alert_label_lbl = lv_label_create(alert_led);
    lv_label_set_text(alert_label_lbl, "OK");
    lv_obj_set_style_text_color(alert_label_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(alert_label_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(alert_label_lbl, &lv_font_montserrat_12, 0);

    status_label = lv_label_create(container);
    lv_label_set_text(status_label, "System Status: Normal");
    lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 70, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
}

/* ============================================================
 * 一级页面：图表
 * ============================================================ */
static void create_chart_section(lv_obj_t *parent)
{
    chart_container = lv_obj_create(parent);
    lv_obj_set_size(chart_container, 390, 310);
    lv_obj_align(chart_container, LV_ALIGN_TOP_LEFT, 380, 40);
    lv_obj_set_style_bg_color(chart_container, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_radius(chart_container, 10, 0);
    lv_obj_set_style_pad_all(chart_container, 0, 0);
    lv_obj_set_scrollbar_mode(chart_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(chart_container, LV_DIR_NONE);

    /* 点击图标进入全屏 */
    lv_obj_add_flag(chart_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chart_container, chart_fullscreen_event_cb, LV_EVENT_CLICKED, NULL);

    chart_title = lv_label_create(chart_container);
    lv_label_set_text(chart_title, "Data Trends");
    lv_obj_set_style_text_font(chart_title, &lv_font_montserrat_16, 0);
    lv_obj_align(chart_title, LV_ALIGN_TOP_MID, 0, 5);

    temp_chart = lv_chart_create(chart_container);
    lv_obj_set_size(temp_chart, 370, 260);
    lv_obj_align(temp_chart, LV_ALIGN_TOP_MID, 0, 32);
    lv_chart_set_type(temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(temp_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(temp_chart, 20);
    lv_chart_set_div_line_count(temp_chart, 5, 10);

    temp_ser = lv_chart_add_series(temp_chart, lv_color_hex(0xFFA500), LV_CHART_AXIS_PRIMARY_Y);
    humidity_ser = lv_chart_add_series(temp_chart, lv_color_hex(0x00BFFF), LV_CHART_AXIS_PRIMARY_Y);
    /* SHIFT 模式：从 index 0 开始画，避免左侧留白 */
    lv_chart_set_value_by_id(temp_chart, temp_ser, 0, LV_CHART_POINT_NONE);
    lv_chart_set_value_by_id(temp_chart, humidity_ser, 0, LV_CHART_POINT_NONE);

    /* 点击图标退出全屏 */
    lv_obj_add_flag(temp_chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(temp_chart, chart_fullscreen_event_cb, LV_EVENT_CLICKED, NULL);

}

/* ============================================================
 * 一级页面：底部 5 个按钮（4 个通道 + 1 个系统占用入口）
 * 屏幕底部 Y≈350~480 一行排开 5 个按钮
 * ============================================================ */
static void create_channel_buttons(lv_obj_t *parent)
{
    /* 底部背景条：占满 790×480 底部 100px */
    channel_buttons_bar = lv_obj_create(parent);
    lv_obj_set_size(channel_buttons_bar, 790, 90);
    lv_obj_align(channel_buttons_bar, LV_ALIGN_BOTTOM_MID, 0, 10);
    lv_obj_set_style_bg_color(channel_buttons_bar, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_style_radius(channel_buttons_bar, 8, 0);
    lv_obj_set_style_pad_all(channel_buttons_bar, 0, 0);
    lv_obj_set_scrollbar_mode(channel_buttons_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(channel_buttons_bar, LV_DIR_NONE);

    lv_obj_t *bar_title = lv_label_create(channel_buttons_bar);
    lv_label_set_text(bar_title, "Tap a sensor / System for details");
    lv_obj_set_style_text_font(bar_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bar_title, lv_color_hex(0x666666), 0);
    lv_obj_align(bar_title, LV_ALIGN_TOP_LEFT, 15, 4);

    /* 5 个按钮：宽 148，高 50，左右留 10，间隔 10，Y=28
     * 计算：5 * 148 + 4 * 10 + 2 * 10 = 800（刚好铺满） */
    static const char *labels[CH_MAX] = { "Temperature", "Humidity", "CO2", "CO" };
    const int16_t btn_w = 148;
    const int16_t btn_h = 50;
    const int16_t gap  = 10;
    const int16_t left = 10;
    for (uint8_t i = 0; i < CH_MAX; i++) {
        lv_obj_t *btn = lv_btn_create(channel_buttons_bar);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, left + (int16_t)i * (btn_w + gap), 28);
        lv_obj_set_style_bg_color(btn, lv_color_hex(chan_info[i].color), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        /* 用 user_data 携带通道号 */
        lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(btn, detail_channel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 第 5 个按钮：系统占用入口 */
    lv_obj_t *sys_btn = lv_btn_create(channel_buttons_bar);
    lv_obj_set_size(sys_btn, btn_w, btn_h);
    lv_obj_align(sys_btn, LV_ALIGN_TOP_LEFT,
                 left + (int16_t)CH_MAX * (btn_w + gap), 28);
    lv_obj_set_style_bg_color(sys_btn, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(sys_btn, 8, 0);
    lv_obj_set_style_pad_all(sys_btn, 0, 0);

    lv_obj_t *sys_btn_lbl = lv_label_create(sys_btn);
    lv_label_set_text(sys_btn_lbl, "System");
    lv_obj_set_style_text_font(sys_btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sys_btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(sys_btn_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(sys_btn, sys_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

/* ============================================================
 * 二级页面（详情页）
 * ============================================================ */
static lv_obj_t *detail_back_btn;
static lv_obj_t *detail_title_lbl;
static lv_obj_t *detail_cur_lbl;      /* 当前值大字号 */
static lv_obj_t *detail_unit_lbl;
static lv_obj_t *detail_avg_12h_lbl;  /* 左列 12h 平均值 */
static lv_obj_t *detail_avg_24h_lbl;  /* 左列 24h 平均值 */
static lv_obj_t *detail_avg30_lbl;    /* 右列 30min 平均值 */
static lv_obj_t *detail_avg60_lbl;    /* 右列 60min 平均值 */
static lv_obj_t *detail_hist_chart;   /* 下半区柱状图 */
static lv_chart_series_t *detail_hist_ser = NULL;   /* 二级页图表 series，首次进入时创建 */

static void detail_back_event_cb(lv_event_t * e)
{
    (void)e;
    /* 隐藏二级页，显示一级页 */
    if (detail_root) lv_obj_add_flag(detail_root, LV_OBJ_FLAG_HIDDEN);
    if (home_root) {
        lv_obj_clear_flag(home_root, LV_OBJ_FLAG_HIDDEN);
        set_flag_recursive(home_root, LV_OBJ_FLAG_HIDDEN, false);
    }
    /* 退出详情页时强制恢复折线图可见性
     * 避免因为 hist_count==0 时被隐藏后，再次进入仍不可见 */
    if (detail_hist_chart) {
        lv_obj_clear_flag(detail_hist_chart, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 点击一级页底部按钮 → 进入二级页 */
static void detail_channel_btn_event_cb(lv_event_t * e)
{
    /* 图表全屏时禁止进入二级页，避免 home_root 被递归隐藏破坏全屏状态 */
    if (chart_fullscreen) return;

    lv_obj_t *btn = lv_event_get_target(e);
    uintptr_t ch = (uintptr_t)lv_obj_get_user_data(btn);
    if (ch >= CH_MAX) return;

    detail_channel = (uint8_t)ch;
    update_detail_page(detail_channel);

    if (home_root) {
        /* 隐藏*/
        set_flag_recursive(home_root, LV_OBJ_FLAG_HIDDEN, true);
    }
    if (detail_root) lv_obj_clear_flag(detail_root, LV_OBJ_FLAG_HIDDEN);
}

/* 刷新二级页面内容 */
static void update_detail_page(uint8_t ch)
{
    if (ch >= CH_MAX) return;

    char buf[64];
    const chan_info_t *ci = &chan_info[ch];

    /* 标题 */
    lv_label_set_text(detail_title_lbl, ci->name);
    lv_obj_set_style_text_color(detail_cur_lbl, lv_color_hex(ci->color), 0);

    /* 当前值：只有喂过实时数据才显示 */
    if (has_current_data) {
        sprintf(buf, "%.1f", chan_get(ch));
        lv_label_set_text(detail_cur_lbl, buf);
    } else {
        lv_label_set_text(detail_cur_lbl, "--");
    }
    lv_label_set_text(detail_unit_lbl, ci->unit);

    /* 30min / 60min / 12h / 24h 平均：没数据时显示 "--" */
    float a30 = -1.0f, a60 = -1.0f, a12h = -1.0f, a24h = -1.0f;
    my_gui_get_average(ch, &a30, &a60, &a12h, &a24h);
    if (a12h < 0.0f) {
        lv_label_set_text(detail_avg_12h_lbl, "--");
    } else {
        char avg_buf[24];
        sprintf(avg_buf, "%.2f %s", a12h, ci->unit);
        lv_label_set_text(detail_avg_12h_lbl, avg_buf);
    }
    if (a24h < 0.0f) {
        lv_label_set_text(detail_avg_24h_lbl, "--");
    } else {
        char avg_buf[24];
        sprintf(avg_buf, "%.2f %s", a24h, ci->unit);
        lv_label_set_text(detail_avg_24h_lbl, avg_buf);
    }
    if (a30 < 0.0f) {
        lv_label_set_text(detail_avg30_lbl, "--");
    } else {
        char avg_buf[24];
        sprintf(avg_buf, "%.2f %s", a30, ci->unit);
        lv_label_set_text(detail_avg30_lbl, avg_buf);
    }
    if (a60 < 0.0f) {
        lv_label_set_text(detail_avg60_lbl, "--");
    } else {
        char avg_buf[24];
        sprintf(avg_buf, "%.2f %s", a60, ci->unit);
        lv_label_set_text(detail_avg60_lbl, avg_buf);
    }

    /* 折线图：仅在 series 还未创建时新建一次，后续只更新值，避免每分钟删建闪烁 */
    lv_chart_set_range(detail_hist_chart, LV_CHART_AXIS_PRIMARY_Y,
                       (lv_coord_t)ci->min, (lv_coord_t)ci->max);
    lv_chart_set_point_count(detail_hist_chart, HISTORY_BUCKETS);

    if (detail_hist_ser == NULL) {
        detail_hist_ser = lv_chart_add_series(detail_hist_chart,
                                              lv_color_hex(ci->color),
                                              LV_CHART_AXIS_PRIMARY_Y);
    } else {
        /* 切换通道时更新颜色，避免残留上一通道的颜色（用 API 兼容 8.1+） */
        lv_chart_set_series_color(detail_hist_chart, detail_hist_ser, lv_color_hex(ci->color));
    }

    uint8_t n = hist_count[ch];

    /* 没数据：折线图整张隐藏 + 系列值全清空 */
    if (n == 0) {
        for (uint8_t i = 0; i < HISTORY_BUCKETS; i++) {
            lv_chart_set_value_by_id(detail_hist_chart, detail_hist_ser, i, LV_CHART_POINT_NONE);
        }
        lv_obj_add_flag(detail_hist_chart, LV_OBJ_FLAG_HIDDEN);
        lv_chart_refresh(detail_hist_chart);
        return;
    }

    /* 有数据：显示折线图，按"从老到新"顺序填入；未采到的位用 NONE（折线断点不连） */
    lv_obj_clear_flag(detail_hist_chart, LV_OBJ_FLAG_HIDDEN);

    /* 先把整条曲线置 NONE，避免残留旧值 */
    for (uint8_t i = 0; i < HISTORY_BUCKETS; i++) {
        lv_chart_set_value_by_id(detail_hist_chart, detail_hist_ser, i, LV_CHART_POINT_NONE);
    }
    uint8_t start = (n < HISTORY_BUCKETS) ? 0 : hist_head[ch];
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (start + i) % HISTORY_BUCKETS;
        lv_coord_t v = (lv_coord_t)history[ch][idx];
        if (v < (lv_coord_t)ci->min) v = (lv_coord_t)ci->min;
        if (v > (lv_coord_t)ci->max) v = (lv_coord_t)ci->max;
        lv_chart_set_value_by_id(detail_hist_chart, detail_hist_ser, i, v);
    }
    lv_chart_refresh(detail_hist_chart);
}

/* 创建二级页面（在屏幕外/隐藏状态下创建） */
static void create_detail_page(lv_obj_t *parent)
{
    detail_root = lv_obj_create(parent);
    lv_obj_set_size(detail_root, 800, 480);
    lv_obj_align(detail_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(detail_root, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(detail_root, 0, 0);
    lv_obj_add_flag(detail_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scroll_dir(detail_root, LV_DIR_NONE);

    /* 顶部条 + 返回 */
    lv_obj_t *header = lv_obj_create(detail_root);
    lv_obj_set_size(header, 800, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);

    detail_back_btn = lv_btn_create(header);
    lv_obj_set_size(detail_back_btn, 70, 25);
    lv_obj_align(detail_back_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(detail_back_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_radius(detail_back_btn, 5, 0);

    lv_obj_t *back_lbl = lv_label_create(detail_back_btn);
    lv_label_set_text(back_lbl, "< BACK");
    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(detail_back_btn, detail_back_event_cb, LV_EVENT_CLICKED, NULL);

    detail_title_lbl = lv_label_create(header);
    lv_label_set_text(detail_title_lbl, "Detail");
    lv_obj_set_style_text_color(detail_title_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(detail_title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(detail_title_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 上半区：当前值 + 12h + 24h 平均（Y=40 ~ Y=240，高 200） */
    lv_obj_t *top = lv_obj_create(detail_root);
    lv_obj_set_size(top, 780, 200);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(top, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_radius(top, 10, 0);
    lv_obj_set_style_border_color(top, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_scrollbar_mode(top, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(top, LV_DIR_NONE);

    /* 左列容器：Current + 12h + 24h 共 3 行（占 380 宽） */
    lv_obj_t *col_cur = lv_obj_create(top);
    lv_obj_set_size(col_cur, 380, 200);
    lv_obj_align(col_cur, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(col_cur, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_cur, 0, 0);
    lv_obj_set_style_pad_all(col_cur, 0, 0);
    lv_obj_set_scrollbar_mode(col_cur, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(col_cur, LV_DIR_NONE);

    /* 左列第1行：表头 "Current" */
    lv_obj_t *cur_header = lv_label_create(col_cur);
    lv_label_set_text(cur_header, "Current");
    lv_obj_set_style_text_font(cur_header, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cur_header, lv_color_hex(0x888888), 0);
    lv_obj_align(cur_header, LV_ALIGN_TOP_LEFT, 20, 8);

    /* 左列第2行：当前值大字 + 单位 */
    detail_cur_lbl = lv_label_create(col_cur);
    lv_label_set_text(detail_cur_lbl, "--");
    lv_obj_set_style_text_font(detail_cur_lbl, &lv_font_montserrat_30, 0);
    lv_obj_align(detail_cur_lbl, LV_ALIGN_TOP_LEFT, 20, 28);

    detail_unit_lbl = lv_label_create(col_cur);
    lv_label_set_text(detail_unit_lbl, "");
    lv_obj_set_style_text_font(detail_unit_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(detail_unit_lbl, lv_color_hex(0x666666), 0);
    lv_obj_align_to(detail_unit_lbl, detail_cur_lbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 80, -4);

    /* 左列第3行：12h 平均 */
    lv_obj_t *row12 = lv_obj_create(col_cur);
    lv_obj_set_size(row12, 360, 50);
    lv_obj_align(row12, LV_ALIGN_TOP_LEFT, 20, 78);
    lv_obj_set_style_bg_opa(row12, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row12, 0, 0);
    lv_obj_set_style_pad_all(row12, 0, 0);
    lv_obj_set_scrollbar_mode(row12, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(row12, LV_DIR_NONE);

    lv_obj_t *row12_name = lv_label_create(row12);
    lv_label_set_text(row12_name, "12h Avg");
    lv_obj_set_style_text_font(row12_name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(row12_name, lv_color_hex(0x888888), 0);
    lv_obj_align(row12_name, LV_ALIGN_LEFT_MID, 0, 0);

    detail_avg_12h_lbl = lv_label_create(row12);
    lv_label_set_text(detail_avg_12h_lbl, "--");
    lv_obj_set_style_text_font(detail_avg_12h_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(detail_avg_12h_lbl, LV_ALIGN_LEFT_MID, 65, 0);

    /* 左列第4行：24h 平均 */
    lv_obj_t *row24 = lv_obj_create(col_cur);
    lv_obj_set_size(row24, 360, 50);
    lv_obj_align(row24, LV_ALIGN_TOP_LEFT, 20, 135);
    lv_obj_set_style_bg_opa(row24, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row24, 0, 0);
    lv_obj_set_style_pad_all(row24, 0, 0);
    lv_obj_set_scrollbar_mode(row24, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(row24, LV_DIR_NONE);

    lv_obj_t *row24_name = lv_label_create(row24);
    lv_label_set_text(row24_name, "24h Avg");
    lv_obj_set_style_text_font(row24_name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(row24_name, lv_color_hex(0x888888), 0);
    lv_obj_align(row24_name, LV_ALIGN_LEFT_MID, 0, 0);

    detail_avg_24h_lbl = lv_label_create(row24);
    lv_label_set_text(detail_avg_24h_lbl, "--");
    lv_obj_set_style_text_font(detail_avg_24h_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(detail_avg_24h_lbl, LV_ALIGN_LEFT_MID, 65, 0);

    /* 右列容器：表头 + 30min/60min 两行（占 380 宽，右对齐） */
    lv_obj_t *col_avg = lv_obj_create(top);
    lv_obj_set_size(col_avg, 380, 200);
    lv_obj_align(col_avg, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(col_avg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_avg, 0, 0);
    lv_obj_set_style_pad_all(col_avg, 0, 0);
    lv_obj_set_scrollbar_mode(col_avg, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(col_avg, LV_DIR_NONE);

    /* 右列表头 */
    lv_obj_t *avg_header = lv_label_create(col_avg);
    lv_label_set_text(avg_header, "Averages");
    lv_obj_set_style_text_font(avg_header, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(avg_header, lv_color_hex(0x888888), 0);
    lv_obj_align(avg_header, LV_ALIGN_TOP_LEFT, 20, 8);

    /* 右列：30min / 60min 两行，固定行高，便于视觉对齐 */
    lv_obj_t *avg30_row = lv_obj_create(col_avg);
    lv_obj_set_size(avg30_row, 360, 55);
    lv_obj_align(avg30_row, LV_ALIGN_TOP_LEFT, 20, 55);
    lv_obj_set_style_bg_opa(avg30_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(avg30_row, 0, 0);
    lv_obj_set_style_pad_all(avg30_row, 0, 0);
    lv_obj_set_scrollbar_mode(avg30_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(avg30_row, LV_DIR_NONE);

    lv_obj_t *avg30_name = lv_label_create(avg30_row);
    lv_label_set_text(avg30_name, "30min");
    lv_obj_set_style_text_font(avg30_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(avg30_name, lv_color_hex(0x888888), 0);
    lv_obj_align(avg30_name, LV_ALIGN_LEFT_MID, 0, 0);

    detail_avg30_lbl = lv_label_create(avg30_row);
    lv_label_set_text(detail_avg30_lbl, "--");
    lv_obj_set_style_text_font(detail_avg30_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(detail_avg30_lbl, LV_ALIGN_LEFT_MID, 55, 0);

    lv_obj_t *avg60_row = lv_obj_create(col_avg);
    lv_obj_set_size(avg60_row, 360, 55);
    lv_obj_align(avg60_row, LV_ALIGN_TOP_LEFT, 20, 120);
    lv_obj_set_style_bg_opa(avg60_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(avg60_row, 0, 0);
    lv_obj_set_style_pad_all(avg60_row, 0, 0);
    lv_obj_set_scrollbar_mode(avg60_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(avg60_row, LV_DIR_NONE);

    lv_obj_t *avg60_name = lv_label_create(avg60_row);
    lv_label_set_text(avg60_name, "60min");
    lv_obj_set_style_text_font(avg60_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(avg60_name, lv_color_hex(0x888888), 0);
    lv_obj_align(avg60_name, LV_ALIGN_LEFT_MID, 0, 0);

    detail_avg60_lbl = lv_label_create(avg60_row);
    lv_label_set_text(detail_avg60_lbl, "--");
    lv_obj_set_style_text_font(detail_avg60_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(detail_avg60_lbl, LV_ALIGN_LEFT_MID, 55, 0);

    /* 左/右列中间画一条分割线（用窄 obj 模拟） */
    lv_obj_t *vline = lv_obj_create(top);
    lv_obj_set_size(vline, 1, 170);
    lv_obj_align(vline, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(vline, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_border_width(vline, 0, 0);

    /* 下半区：折线图历史（Y=250 ~ Y=480，高 220） */
    lv_obj_t *bot = lv_obj_create(detail_root);
    lv_obj_set_size(bot, 780, 200);
    lv_obj_align(bot, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_style_bg_color(bot, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_radius(bot, 10, 0);
    lv_obj_set_scrollbar_mode(bot, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(bot, LV_DIR_NONE);

    lv_obj_t *bot_title = lv_label_create(bot);
    lv_label_set_text(bot_title, "History (1h, 1 sample/min)");
    lv_obj_set_style_text_font(bot_title, &lv_font_montserrat_14, 0);
    lv_obj_align(bot_title, LV_ALIGN_TOP_LEFT, 10, 5);

    detail_hist_chart = lv_chart_create(bot);
    lv_obj_set_size(detail_hist_chart, 760, 180);
    lv_obj_align(detail_hist_chart, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_chart_set_type(detail_hist_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(detail_hist_chart, 5, 6);
    lv_chart_set_point_count(detail_hist_chart, HISTORY_BUCKETS);
    /* 折线图加粗一点更好看 */
    lv_obj_set_style_line_width(detail_hist_chart, 2, LV_PART_ITEMS);
    /* 上电未喂数据时：折线图区域整体隐藏 */
    lv_obj_add_flag(detail_hist_chart, LV_OBJ_FLAG_HIDDEN);
}

/* ============================================================
 * 系统占用页（与 detail_root 平级，靠 HIDDEN 切换）
 * ============================================================ */
static lv_obj_t *sys_back_btn;
static lv_obj_t *sys_title_lbl;
static lv_obj_t *sys_uptime_lbl;
static lv_obj_t *sys_cpu_lbl;
static lv_obj_t *sys_heap_lbl;
static lv_obj_t *sys_tasks_lbl;
static lv_obj_t *sys_lvgl_mem_lbl;

/* 一个简单的进度条：模拟 CPU 使用率 */
static lv_obj_t *sys_cpu_bar;
static lv_obj_t *sys_cpu_chart;   /* CPU 10秒折线图 */
static lv_chart_series_t *sys_cpu_ser;  /* CPU 折线图数据序列 */
/* 一行任务条目的容器（动态填充前 8 个任务） */
static lv_obj_t *sys_tasks_box;

/* CPU 占用率 1 分钟均值缓冲
 * 刷新周期 500ms → 120 槽位 = 60 秒历史
 * buf[0] = 最新，buf[count-1] = 最旧 */
#define CPU_BUF_SIZE  20
static uint8_t cpu_hist_buf[CPU_BUF_SIZE];   /* 每个元素 = 0~100% */
static uint8_t cpu_hist_head = 0;
static uint8_t cpu_hist_count = 0;

/* 从环形缓冲计算均值 */
static uint8_t cpu_avg_1min(void)
{
    if (cpu_hist_count == 0) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < cpu_hist_count; i++) {
        sum += cpu_hist_buf[i];
    }
    return (uint8_t)(sum / cpu_hist_count);
}

/* FreeRTOS CPU% 计算
 *  - 每任务累加 ulRunTimeCounter → 总 CPU 时间
 *  - CPU% = 100 - IdleTask% - (总系统开销忽略不计)
 *  - 第一次调用时 total_runtime=0 需要跳过（等待计数器走起来） */
static uint32_t last_total_runtime = 0;   /* 上一次的总运行时间（用于计算差值） */
static uint32_t last_idle_runtime  = 0;   /* 上一次的 Idle 运行时间 */

static uint8_t sys_calc_cpu_pct(void)
{
    /* 等计数器跑起来：FreeRTOSRunTimeTicks 至少要有几个 ms */
    uint32_t total_runtime = portGET_RUN_TIME_COUNTER_VALUE();
    if (total_runtime < 1000U) return 0;     /* < 1ms，跳过（防止除 0 或太小的分母） */

    /* 获取所有任务状态 */
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0) return 0;

    /* 用栈上动态分配（n 最大几百，上层调用不频繁，安全） */
    TaskStatus_t *arr = pvPortMalloc(n * sizeof(TaskStatus_t));
    if (arr == NULL) return 0;

    uint32_t got = uxTaskGetSystemState(arr, n, NULL);
    uint32_t idle_time = 0;
    for (uint32_t i = 0; i < got; i++) {
        if (strncmp(arr[i].pcTaskName, "IDLE", 4) == 0) {
            idle_time = arr[i].ulRunTimeCounter;
            break;
        }
    }

    vPortFree(arr);

    /* 总时间差 = 本次 - 上次（避免计数器溢出影响，溢出时 last_total_runtime 重置） */
    uint32_t delta_total = (total_runtime >= last_total_runtime)
                           ? (total_runtime - last_total_runtime) : total_runtime;
    uint32_t delta_idle  = (idle_time >= last_idle_runtime)
                            ? (idle_time - last_idle_runtime) : idle_time;

    last_total_runtime = total_runtime;
    last_idle_runtime  = idle_time;

    if (delta_total == 0) return 0;

    /* CPU% = 100 - (Idle 占用的比例) */
    uint32_t cpu = 100U - ((delta_idle * 100U) / delta_total);
    if (cpu > 100U) cpu = 100U;
    return (uint8_t)cpu;
}

static void update_system_page(void)
{
    if (!sys_root) return;
    char buf[64];

    /* 运行时间 */
    uint32_t sec = HAL_GetTick() / 1000U;
    uint32_t h = sec / 3600U;
    uint32_t m = (sec % 3600U) / 60U;
    uint32_t s = sec % 60U;
    sprintf(buf, "%uh %02um %02us", h, m, s);
    lv_label_set_text(sys_uptime_lbl, buf);

    /* CPU 占用（1 分钟均值） */
    uint8_t cpu = cpu_avg_1min();
    sprintf(buf, "%u %%", cpu);
    lv_label_set_text(sys_cpu_lbl, buf);
    if (sys_cpu_bar) lv_bar_set_value(sys_cpu_bar, cpu, LV_ANIM_ON);

    /* CPU 折线图：同步 cpu_hist_buf → LVGL 图表（最旧→最新） */
    if (sys_cpu_chart && sys_cpu_ser) {
        uint8_t oldest = (cpu_hist_count < CPU_BUF_SIZE) ? 0 : cpu_hist_head;
        for (uint8_t i = 0; i < cpu_hist_count; i++) {
            uint8_t idx = (oldest + i) % CPU_BUF_SIZE;
            lv_chart_set_value_by_id(sys_cpu_chart, sys_cpu_ser, i, cpu_hist_buf[idx]);
        }
        lv_chart_refresh(sys_cpu_chart);
    }

    /* Heap */
    size_t free_heap  = xPortGetFreeHeapSize();
    size_t total_heap = configTOTAL_HEAP_SIZE;
    size_t min_free   = xPortGetMinimumEverFreeHeapSize();
    sprintf(buf, "Used %u / %u B  (Min free ever %u B)",
            (unsigned)(total_heap - free_heap), (unsigned)total_heap, (unsigned)min_free);
    lv_label_set_text(sys_heap_lbl, buf);

    /* 任务数 + 任务列表 */
#if (configUSE_TRACE_FACILITY == 1)
    UBaseType_t n = uxTaskGetNumberOfTasks();
    sprintf(buf, "%u task(s)", (unsigned)n);
#else
    sprintf(buf, "n/a");
#endif
    lv_label_set_text(sys_tasks_lbl, buf);

    /* 列出前 8 个任务的 stack 高水位（最重耗时） */
#if (configUSE_TRACE_FACILITY == 1) && (configUSE_STATS_FORMATTING_FUNCTIONS == 1)
    TaskStatus_t arr[8];
    UBaseType_t got = uxTaskGetSystemState(arr, 8, NULL);
    if (sys_tasks_box) {
        /* 清空旧内容 */
        uint32_t cnt = lv_obj_get_child_cnt(sys_tasks_box);
        while (cnt--) lv_obj_del(lv_obj_get_child(sys_tasks_box, cnt));
        for (UBaseType_t i = 0; i < got; i++) {
            char name[24];
            snprintf(name, sizeof(name), "%-10s %u",
                     arr[i].pcTaskName, (unsigned)arr[i].usStackHighWaterMark);
            lv_obj_t *l = lv_label_create(sys_tasks_box);
            lv_label_set_text(l, name);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(0x222222), 0);
        }
    }
#endif

    /* LVGL 内存使用 */
    lv_mem_monitor_t mem;
    lv_mem_monitor(&mem);
    sprintf(buf, "LVGL total %u B  free %u B  used %u B  frag %u%%",
            (unsigned)mem.total_size, (unsigned)mem.free_size,
            (unsigned)(mem.total_size - mem.free_size),
            (unsigned)mem.frag_pct);
    lv_label_set_text(sys_lvgl_mem_lbl, buf);
}

static void sys_back_event_cb(lv_event_t * e)
{
    (void)e;
    if (sys_root) lv_obj_add_flag(sys_root, LV_OBJ_FLAG_HIDDEN);
    if (home_root) {
        lv_obj_clear_flag(home_root, LV_OBJ_FLAG_HIDDEN);
        set_flag_recursive(home_root, LV_OBJ_FLAG_HIDDEN, false);
    }
    /* 退出系统页后强制恢复 chart 可见性（同 detail 逻辑） */
    if (detail_hist_chart) {
        lv_obj_clear_flag(detail_hist_chart, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sys_btn_event_cb(lv_event_t * e)
{
    (void)e;
    if (chart_fullscreen) return;
    update_system_page();
    if (home_root) set_flag_recursive(home_root, LV_OBJ_FLAG_HIDDEN, true);
    if (sys_root)  lv_obj_clear_flag(sys_root, LV_OBJ_FLAG_HIDDEN);
}

static void create_system_page(lv_obj_t *parent)
{
    sys_root = lv_obj_create(parent);
    lv_obj_set_size(sys_root, 800, 480);
    lv_obj_align(sys_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(sys_root, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(sys_root, 0, 0);
    lv_obj_add_flag(sys_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scroll_dir(sys_root, LV_DIR_NONE);

    /* 顶部条 + 返回 */
    lv_obj_t *header = lv_obj_create(sys_root);
    lv_obj_set_size(header, 800, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);

    sys_back_btn = lv_btn_create(header);
    lv_obj_set_size(sys_back_btn, 70, 25);
    lv_obj_align(sys_back_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(sys_back_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_radius(sys_back_btn, 5, 0);

    lv_obj_t *back_lbl = lv_label_create(sys_back_btn);
    lv_label_set_text(back_lbl, "< BACK");
    lv_obj_align(back_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(sys_back_btn, sys_back_event_cb, LV_EVENT_CLICKED, NULL);

    sys_title_lbl = lv_label_create(header);
    lv_label_set_text(sys_title_lbl, "System Status");
    lv_obj_set_style_text_color(sys_title_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(sys_title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(sys_title_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 内容面板：Y=40 起，整块 780×420 */
    lv_obj_t *panel = lv_obj_create(sys_root);
    lv_obj_set_size(panel, 780, 410);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(panel, LV_DIR_NONE);

    /* ---- 行 1：Uptime ---- */
    lv_obj_t *r1 = lv_obj_create(panel);
    lv_obj_set_size(r1, 740, 50);
    lv_obj_align(r1, LV_ALIGN_TOP_LEFT, 20, 15);
    lv_obj_set_style_bg_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r1, 0, 0);
    lv_obj_set_scrollbar_mode(r1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(r1, LV_DIR_NONE);

    lv_obj_t *r1_lbl = lv_label_create(r1);
    lv_label_set_text(r1_lbl, "Uptime");
    lv_obj_set_style_text_font(r1_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r1_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(r1_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    sys_uptime_lbl = lv_label_create(r1);
    lv_label_set_text(sys_uptime_lbl, "--");
    lv_obj_set_style_text_font(sys_uptime_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sys_uptime_lbl, lv_color_hex(0x222222), 0);
    lv_obj_align(sys_uptime_lbl, LV_ALIGN_LEFT_MID, 120, 0);

    /* ---- 行 2：CPU 占用（10秒均值，仅数值标签）---- */
    lv_obj_t *r2 = lv_obj_create(panel);
    lv_obj_set_size(r2, 740, 50);
    lv_obj_align(r2, LV_ALIGN_TOP_LEFT, 20, 75);
    lv_obj_set_style_bg_opa(r2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r2, 0, 0);
    lv_obj_set_scrollbar_mode(r2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(r2, LV_DIR_NONE);

    lv_obj_t *r2_lbl = lv_label_create(r2);
    lv_label_set_text(r2_lbl, "CPU Load (10s Avg)");
    lv_obj_set_style_text_font(r2_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r2_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(r2_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    sys_cpu_lbl = lv_label_create(r2);
    lv_label_set_text(sys_cpu_lbl, "--");
    lv_obj_set_style_text_font(sys_cpu_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sys_cpu_lbl, lv_color_hex(0x00B050), 0);
    lv_obj_align(sys_cpu_lbl, LV_ALIGN_LEFT_MID, 150, 0);

    /* ---- 行 2b：CPU 进度条（单独一行）---- */
    lv_obj_t *r2b = lv_obj_create(panel);
    lv_obj_set_size(r2b, 680, 25);
    lv_obj_align(r2b, LV_ALIGN_TOP_LEFT, 20, 145);
    lv_obj_set_style_bg_opa(r2b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r2b, 0, 0);
    lv_obj_set_scrollbar_mode(r2b, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(r2b, LV_DIR_NONE);

    sys_cpu_bar = lv_bar_create(r2b);
    lv_obj_set_size(sys_cpu_bar, 680, 12);
    lv_obj_align(sys_cpu_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(sys_cpu_bar, 0, 100);
    lv_bar_set_value(sys_cpu_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sys_cpu_bar, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sys_cpu_bar, lv_color_hex(0x00B050), LV_PART_INDICATOR);

    /* ---- 行 3：Heap 占用 ---- */
    lv_obj_t *r3 = lv_obj_create(panel);
    lv_obj_set_size(r3, 740, 40);
    lv_obj_align(r3, LV_ALIGN_TOP_LEFT, 20, 170);
    lv_obj_set_style_bg_opa(r3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r3, 0, 0);
    lv_obj_set_scrollbar_mode(r3, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(r3, LV_DIR_NONE);

    lv_obj_t *r3_lbl = lv_label_create(r3);
    lv_label_set_text(r3_lbl, "Heap (FreeRTOS)");
    lv_obj_set_style_text_font(r3_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r3_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(r3_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    sys_heap_lbl = lv_label_create(r3);
    lv_label_set_text(sys_heap_lbl, "--");
    lv_obj_set_style_text_font(sys_heap_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sys_heap_lbl, lv_color_hex(0x222222), 0);
    lv_obj_align(sys_heap_lbl, LV_ALIGN_LEFT_MID, 120, 0);

    /* ---- 行 4：任务数 ---- */
    lv_obj_t *r4 = lv_obj_create(panel);
    lv_obj_set_size(r4, 740, 40);
    lv_obj_align(r4, LV_ALIGN_TOP_LEFT, 20, 220);
    lv_obj_set_style_bg_opa(r4, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r4, 0, 0);
    lv_obj_set_scrollbar_mode(r4, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(r4, LV_DIR_NONE);

    lv_obj_t *r4_lbl = lv_label_create(r4);
    lv_label_set_text(r4_lbl, "Tasks");
    lv_obj_set_style_text_font(r4_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r4_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(r4_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    sys_tasks_lbl = lv_label_create(r4);
    lv_label_set_text(sys_tasks_lbl, "--");
    lv_obj_set_style_text_font(sys_tasks_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(sys_tasks_lbl, LV_ALIGN_LEFT_MID, 120, 0);

    /* 任务条目盒子 */
    sys_tasks_box = lv_obj_create(panel);
    lv_obj_set_size(sys_tasks_box, 740, 50);
    lv_obj_align(sys_tasks_box, LV_ALIGN_TOP_LEFT, 20, 265);
    lv_obj_set_style_bg_opa(sys_tasks_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sys_tasks_box, 0, 0);
    lv_obj_set_scrollbar_mode(sys_tasks_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(sys_tasks_box, LV_DIR_NONE);

    /* ---- CPU 10秒折线图 ---- */
    sys_cpu_chart = lv_chart_create(panel);
    lv_obj_set_size(sys_cpu_chart, 700, 55);
    lv_obj_align(sys_cpu_chart, LV_ALIGN_TOP_LEFT, 20, 320);
    lv_chart_set_type(sys_cpu_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(sys_cpu_chart, 3, 10);
    lv_chart_set_point_count(sys_cpu_chart, CPU_BUF_SIZE);
    lv_chart_set_range(sys_cpu_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(sys_cpu_chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_set_style_line_width(sys_cpu_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(sys_cpu_chart, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(sys_cpu_chart, lv_color_hex(0x444444), 0);
    sys_cpu_ser = lv_chart_add_series(sys_cpu_chart, lv_color_hex(0x00B050), LV_CHART_AXIS_PRIMARY_Y);

    /* ---- 行 5：LVGL 内存 ---- */
    lv_obj_t *r5 = lv_obj_create(panel);
    lv_obj_set_size(r5, 740, 40);
    lv_obj_align(r5, LV_ALIGN_TOP_LEFT, 20, 380);
    lv_obj_set_style_bg_opa(r5, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r5, 0, 0);
    lv_obj_set_scrollbar_mode(r5, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(r5, LV_DIR_NONE);

    lv_obj_t *r5_lbl = lv_label_create(r5);
    lv_label_set_text(r5_lbl, "LVGL Memory");
    lv_obj_set_style_text_font(r5_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r5_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(r5_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    sys_lvgl_mem_lbl = lv_label_create(r5);
    lv_label_set_text(sys_lvgl_mem_lbl, "--");
    lv_obj_set_style_text_font(sys_lvgl_mem_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sys_lvgl_mem_lbl, lv_color_hex(0x222222), 0);
    lv_obj_align(sys_lvgl_mem_lbl, LV_ALIGN_LEFT_MID, 120, 0);
}

/* ============================================================
 * 主入口
 * ============================================================ */
/* 周期性刷新系统页（500ms 一次，独立于主 pump）
 * lv_timer_create 由 LVGL 自身 tick 驱动，线程模型安全 */
static void sys_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!lv_obj_is_visible(sys_root)) return;

    /* 每 500ms 采集一次瞬时 CPU%，推入 60 秒均值缓冲 */
    uint8_t instant = sys_calc_cpu_pct();
    cpu_hist_buf[cpu_hist_head] = instant;
    cpu_hist_head = (cpu_hist_head + 1) % CPU_BUF_SIZE;
    if (cpu_hist_count < CPU_BUF_SIZE) cpu_hist_count++;

    update_system_page();
}

void my_gui(void)
{
    history_init();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);

    /* 一级页根（用来整体隐藏/显示） */
    home_root = lv_obj_create(scr);
    lv_obj_set_size(home_root, 800, 480);
    lv_obj_align(home_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(home_root, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(home_root, 0, 0);
    lv_obj_set_scrollbar_mode(home_root, LV_SCROLLBAR_MODE_OFF);       // 设置滚动条
    lv_obj_set_scroll_dir(home_root, LV_DIR_NONE);                                      // 设置滚动方向 无

    /* 标题栏 */
    lv_obj_t *home_label = lv_obj_create(home_root);
    lv_obj_set_size(home_label, 800, 30);
    lv_obj_set_style_bg_color(home_label, lv_color_hex(0x333333), 0);
    lv_obj_align(home_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_scrollbar_mode(home_label, LV_SCROLLBAR_MODE_OFF);

    home_label_title = lv_label_create(home_label);
    lv_label_set_text(home_label_title, "Grain Warehouse Environment Monitoring System");
    lv_obj_set_style_text_color(home_label_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(home_label_title, LV_ALIGN_CENTER, 0, 0);

    /* 各功能区 */
    create_sensor_section(home_root);
    create_fan_control_section(home_root);
    create_alert_section(home_root);
    create_chart_section(home_root);
    create_channel_buttons(home_root);

    /* 二级页（在 home_root 之外独立存在，与一级页同级，靠隐藏切换） */
    create_detail_page(scr);

    /* 系统占用页（与 detail_root 平级） */
    create_system_page(scr);

    update_sensor_display();
    update_chart_data();

    /* 周期刷新系统占用页（独立于 pump） */
    lv_timer_create(sys_refresh_timer_cb, 500, NULL);
}
