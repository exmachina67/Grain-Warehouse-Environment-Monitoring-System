/**
 ****************************************************************************************************
 * @file        btim.c
 * @author      姝ｇ偣鍘熷瓙鍥㈤槦(ALIENTEK)
 * @version     V1.0
 * @date        2021-10-15
 * @brief       鍩烘湰瀹氭椂鍣� 椹卞姩浠ｇ爜
 * @license     Copyright (c) 2020-2032, 骞垮窞甯傛槦缈肩數瀛愮�戞妧鏈夐檺鍏�鍙�
 ****************************************************************************************************
 * @attention
 *
 * 瀹為獙骞冲彴:姝ｇ偣鍘熷瓙 STM32F407寮€鍙戞澘
 * 鍦ㄧ嚎瑙嗛��:www.yuanzige.com
 * 鎶€鏈�璁哄潧:www.openedv.com
 * 鍏�鍙哥綉鍧€:www.alientek.com
 * 璐�涔板湴鍧€:openedv.taobao.com
 *
 * 淇�鏀硅�存槑
 * V1.0 20211015
 * 绗�涓€娆″彂甯�
 *
 ****************************************************************************************************
 */

#include "./BSP/TIMER/btim.h"
#include "lvgl.h"

TIM_HandleTypeDef g_timx_handle;         /* 瀹氭椂鍣ㄥ弬鏁板彞鏌� */

/* FreeRTOS CPU 统计用计数器（TIM6 溢出次数，低32位由硬件自动累加） */
volatile uint32_t FreeRTOSRunTimeTicks = 0;

/**
 * @brief       鍩烘湰瀹氭椂鍣═IMX瀹氭椂涓�鏂�鍒濆�嬪寲鍑芥暟
 * @note
 *              鍩烘湰瀹氭椂鍣ㄧ殑鏃堕挓鏉ヨ嚜APB1,褰揚PRE1 鈮� 2鍒嗛�戠殑鏃跺€�
 *              鍩烘湰瀹氭椂鍣ㄧ殑鏃堕挓涓篈PB1鏃堕挓鐨�2鍊�, 鑰孉PB1涓�42M, 鎵€浠ュ畾鏃跺櫒鏃堕挓 = 84Mhz
 *              瀹氭椂鍣ㄦ孩鍑烘椂闂磋�＄畻鏂规硶: Tout = ((arr + 1) * (psc + 1)) / Ft us.
 *              Ft=瀹氭椂鍣ㄥ伐浣滈�戠巼,鍗曚綅:Mhz
 *
 * @param       arr : 鑷�鍔ㄩ噸瑁呭€笺€�
 * @param       psc : 鏃堕挓棰勫垎棰戞暟
 * @retval      鏃�
 */
void btim_timx_int_init(uint16_t arr, uint16_t psc)
{
    g_timx_handle.Instance = BTIM_TIMX_INT;                      /* 瀹氭椂鍣▁ */
    g_timx_handle.Init.Prescaler = psc;                          /* 鍒嗛�戠郴鏁� */
    g_timx_handle.Init.CounterMode = TIM_COUNTERMODE_UP;         /* 閫掑�炶�℃暟妯″紡 */
    g_timx_handle.Init.Period = arr;                             /* 鑷�鍔ㄨ�呰浇鍊� */
    HAL_TIM_Base_Init(&g_timx_handle);
    
    HAL_TIM_Base_Start_IT(&g_timx_handle);                       /* 浣胯兘瀹氭椂鍣▁鍜屽畾鏃跺櫒鏇存柊涓�鏂� */
}

/**
 * @brief       瀹氭椂鍣ㄥ簳灞傞┍鍔�锛屽紑鍚�鏃堕挓锛岃�剧疆涓�鏂�浼樺厛绾�
                姝ゅ嚱鏁颁細琚獺AL_TIM_Base_Init()鍑芥暟璋冪敤
 * @param       鏃�
 * @retval      鏃�
 */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == BTIM_TIMX_INT)
    {
        BTIM_TIMX_INT_CLK_ENABLE();                     /* 浣胯兘TIMx鏃堕挓 */
        HAL_NVIC_SetPriority(BTIM_TIMX_INT_IRQn, 1, 3); /* 鎶㈠崰1锛屽瓙浼樺厛绾�3 */
        HAL_NVIC_EnableIRQ(BTIM_TIMX_INT_IRQn);         /* 寮€鍚疘TMx涓�鏂� */
    }
}

/**
 * @brief       鍩烘湰瀹氭椂鍣═IMX涓�鏂�鏈嶅姟鍑芥暟
 * @param       鏃�
 * @retval      鏃�
 */
void BTIM_TIMX_INT_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_timx_handle);  /* 瀹氭椂鍣ㄥ洖璋冨嚱鏁� */
}

/**
 * @brief       鍥炶皟鍑芥暟锛屽畾鏃跺櫒涓�鏂�鏈嶅姟鍑芥暟璋冪敤
 * @param       鏃�
 * @retval      鏃�
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == BTIM_TIMX_INT)
    {
        FreeRTOSRunTimeTicks++;
    }
}

/**
 * @brief       初�?�化 TIM6 计数噡，供库�?.
 * @note        用freertos在my_gui初�?�后调用�?
 *              颓形后车池为 1680-1�?时形为CLK/100 = 1.68MHz�?
 *              16位模�?65536 / 1.68MHz 长形周期�?
 * @retval      �?
 */
void ConfigureTimeForRunTimeStats(void)
{
    /* TIM6: arr=1680-1, psc=100-1  计数�?ms�?一次，与单狯Tick同��?
     * 实时性表现准�?1%以内 */
    btim_timx_int_init(1680 - 1, 100 - 1);
}
