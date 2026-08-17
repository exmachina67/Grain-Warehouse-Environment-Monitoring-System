/**
 ****************************************************************************************************
 * @file        task_sys_monitor.c
 * @author      010
 * @version     V1.0
 * @date        2022-01-11
 * @brief       系统监控任务 — vTaskList / CPU占用 / 堆水位，串口输出 + 投递 GUI 队列
 *
 * 工作职责：
 *  - 采集各任务状态（vTaskList）          → 串口表格
 *  - 采集各任务 CPU 占用（vTaskGetRunTimeStats）→ 串口表格
 *  - 采集堆水位（xPortGetFreeHeapSize / MinimumEverFreeHeapSize）
 *  - 投递结构化 sys_status_t 到 SysMonitorQueue（GUI 任务消费）
 *
 * 优先级 2，最低，仅在不抢业务时跑
 ****************************************************************************************************
 */

#include "FreeRtos/Task/task_sys_monitor/task_sys_monitor.h"
#include "FreeRTOS_tasks.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/*============================================ 私有变量 ============================================*/
#define SYS_MONITOR_VTABLE_BUF   1024

static QueueHandle_t s_sys_mon_queue   = NULL;
static char         s_vtable_buf[SYS_MONITOR_VTABLE_BUF];
static TaskStatus_t s_task_status[16];

/*============================================ 公共接口 ============================================*/

QueueHandle_t SysMonitorQueue(void)
{
    return s_sys_mon_queue;
}

void SysMonitorInit(void)
{
    if (s_sys_mon_queue == NULL) {
        s_sys_mon_queue = xQueueCreate(1, sizeof(sys_status_t));
        configASSERT(s_sys_mon_queue != NULL);
    }
}

/*============================================ 私有工具函数 ============================================*/

/**
 * @brief  解析 vTaskList 输出表格，提取每个任务名
 */
static uint8_t parse_vtable_names(const char *table, sys_status_t *out)
{
    out->task_count = 0;
    if (table == NULL) return 0;

    uint8_t count = 0;
    const char *p = table;
    uint8_t line_no = 0;

    while (*p && count < 16) {
        const char *eol = strchr(p, '\n');
        size_t line_len = (eol == NULL) ? strlen(p) : (size_t)(eol - p);

        if (line_no >= 1 && line_len > 0) {
            const char *first = p;
            while (*first == ' ' && first < p + line_len) first++;
            if (first < p + line_len) {
                size_t i = 0;
                while (i < line_len && p[i] != ' ' && i < sizeof(out->tasks[0].name) - 1) {
                    out->tasks[count].name[i] = p[i];
                    i++;
                }
                out->tasks[count].name[i] = '\0';
                count++;
            }
        }
        line_no++;
        if (eol == NULL) break;
        p = eol + 1;
    }

    out->task_count = count;
    return count;
}

/**
 * @brief  通过 uxTaskGetSystemState 拿每个任务的详细状态
 */
static void fill_task_stats(sys_status_t *status)
{
    UBaseType_t num = uxTaskGetNumberOfTasks();
    if (num > 16) num = 16;

    UBaseType_t got = uxTaskGetSystemState(s_task_status, num, NULL);
    if (got == 0) return;

    for (UBaseType_t i = 0; i < status->task_count; i++) {
        for (UBaseType_t j = 0; j < got; j++) {
            if (strncmp(status->tasks[i].name,
                        s_task_status[j].pcTaskName,
                        configMAX_TASK_NAME_LEN) == 0) {
                status->tasks[i].prio             = s_task_status[j].uxCurrentPriority;
                status->tasks[i].state            = (UBaseType_t)s_task_status[j].eCurrentState;
                status->tasks[i].stack_free_words = s_task_status[j].usStackHighWaterMark;
                status->tasks[i].run_time_ticks   = (uint32_t)s_task_status[j].ulRunTimeCounter;
                status->tasks[i].cpu_pct          = 0.0f;
                break;
            }
        }
    }
}

/**
 * @brief  解析 vTaskGetRunTimeStats 输出，提取 CPU 占用百分比
 */
static void parse_cpu_pct(const char *table, sys_status_t *status)
{
    if (table == NULL) return;

    const char *p = table;
    uint8_t line_no = 0;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t line_len = (eol == NULL) ? strlen(p) : (size_t)(eol - p);

        if (line_no >= 2 && line_len > 0) {
            const char *q = p;
            while (*q == ' ' && q < p + line_len) q++;
            if (q < p + line_len) {
                char name[16] = {0};
                size_t i = 0;
                while (i < line_len && p[i] != ' ' && i < sizeof(name) - 1) {
                    name[i] = p[i];
                    i++;
                }

                const char *pct = NULL;
                for (size_t k = line_len; k > 0; k--) {
                    if (p[k - 1] == '%') { pct = &p[k - 1]; break; }
                }
                if (pct != NULL) {
                    char numbuf[8] = {0};
                    size_t ni = 0;
                    int j = (int)(pct - p) - 1;
                    while (j >= 0 && p[j] == ' ' && ni < sizeof(numbuf) - 1) j--;
                    while (j >= 0 && ni < sizeof(numbuf) - 1) {
                        if (p[j] >= '0' && p[j] <= '9') {
                            numbuf[ni++] = p[j--];
                        } else break;
                    }
                    for (size_t a = 0, b = ni; a < b; a++, b--) {
                        char t = numbuf[a]; numbuf[a] = numbuf[b - 1]; numbuf[b - 1] = t;
                    }
                    float pct_val = (ni > 0) ? (float)atoi(numbuf) : 0.0f;

                    for (UBaseType_t t = 0; t < status->task_count; t++) {
                        if (strncmp(status->tasks[t].name, name, configMAX_TASK_NAME_LEN) == 0) {
                            status->tasks[t].cpu_pct = pct_val;
                            break;
                        }
                    }
                }
            }
        }
        line_no++;
        if (eol == NULL) break;
        p = eol + 1;
    }
}

/*============================================ 任务函数 ============================================*/

void sys_monitor_task(void *pvParameters)
{
    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(2000));   /* 等其他任务创建完成 */

    printf("[SYS-MON] start (period=%dms)\r\n", (int)SYS_MONITOR_PERIOD_MS);

    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SYS_MONITOR_PERIOD_MS));

        /* ---- 1) vTaskList ---- */
        memset(s_vtable_buf, 0, sizeof(s_vtable_buf));
        vTaskList(s_vtable_buf);
        printf("Task            State   Prio    Stack    Num\r\n");
        printf("===========================================\r\n");
        printf("%s\r\n", s_vtable_buf);

        char list_buf[SYS_MONITOR_VTABLE_BUF];
        strncpy(list_buf, s_vtable_buf, sizeof(list_buf) - 1);
        list_buf[sizeof(list_buf) - 1] = '\0';

        /* ---- 2) vTaskGetRunTimeStats ---- */
#if (configGENERATE_RUN_TIME_STATS == 1)
        memset(s_vtable_buf, 0, sizeof(s_vtable_buf));
        vTaskGetRunTimeStats(s_vtable_buf);
        printf("Task            Abs Time        %% Time\r\n");
        printf("******************************************\r\n");
        printf("%s\r\n", s_vtable_buf);
#endif

        /* ---- 3) 拼装 sys_status_t ---- */
        sys_status_t status;
        memset(&status, 0, sizeof(status));

        parse_vtable_names(list_buf, &status);
        fill_task_stats(&status);
#if (configGENERATE_RUN_TIME_STATS == 1)
        parse_cpu_pct(s_vtable_buf, &status);
#endif

        status.heap_free_bytes     = (uint32_t)xPortGetFreeHeapSize();
        status.heap_min_free_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
        status.tick_ms            = (uint32_t)xTaskGetTickCount();

        if (s_sys_mon_queue != NULL) {
            xQueueSend(s_sys_mon_queue, &status, 0);
        }

        printf("[SYS] heap free=%lu min=%lu tick=%lu\r\n\r\n",
               (unsigned long)status.heap_free_bytes,
               (unsigned long)status.heap_min_free_bytes,
               (unsigned long)status.tick_ms);
    }
}
