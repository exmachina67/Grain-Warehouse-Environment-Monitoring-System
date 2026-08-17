#include "./BSP/motor/gtim.h"
#include "./BSP/led/led.h"
#include <stdio.h>


TIM_HandleTypeDef g_timg_handle;            


void gtim_timx_int_init(uint16_t arr, uint16_t psc)
{
    GTIM_TIMX_INT_CLK_ENABLE(); 

    g_timg_handle.Instance = GTIM_TIMX_INT;                
    g_timg_handle.Init.Prescaler = psc;                    
    g_timg_handle.Init.CounterMode = TIM_COUNTERMODE_UP;    
    g_timg_handle.Init.Period = arr;                        
    HAL_TIM_Base_Init(&g_timg_handle);
    
    HAL_NVIC_SetPriority(GTIM_TIMX_INT_IRQn, 1, 3);         
    HAL_NVIC_EnableIRQ(GTIM_TIMX_INT_IRQn);                

    HAL_TIM_Base_Start_IT(&g_timg_handle);                  
}


TIM_HandleTypeDef g_timx_pwm_chy_handle;     /* ��ʱ��x��� */


void gtim_timx_pwm_chy_init(uint16_t arr, uint16_t psc)
{
    TIM_OC_InitTypeDef timx_oc_pwm_chy = {0};                       
    
    g_timx_pwm_chy_handle.Instance = GTIM_TIMX_PWM;                 
    g_timx_pwm_chy_handle.Init.Prescaler = psc;                    
    g_timx_pwm_chy_handle.Init.CounterMode = TIM_COUNTERMODE_UP;    
    g_timx_pwm_chy_handle.Init.Period = arr;                        
    
    // HAL底层初始化前先手动初始化GPIO
    GTIM_PWM_MspInit(&g_timx_pwm_chy_handle);
    
    HAL_TIM_PWM_Init(&g_timx_pwm_chy_handle);                       

    timx_oc_pwm_chy.OCMode = TIM_OCMODE_PWM1;                       
    timx_oc_pwm_chy.Pulse = arr / 2;                            

    timx_oc_pwm_chy.OCPolarity = TIM_OCPOLARITY_HIGH;                                      
    HAL_TIM_PWM_ConfigChannel(&g_timx_pwm_chy_handle, &timx_oc_pwm_chy, GTIM_TIMX_PWM_CHY);     // CH1
    HAL_TIM_PWM_Start(&g_timx_pwm_chy_handle, GTIM_TIMX_PWM_CHY);      

    HAL_TIM_PWM_ConfigChannel(&g_timx_pwm_chy_handle, &timx_oc_pwm_chy, GTIM_TIMX_PWM_CH2);     // CH2
    HAL_TIM_PWM_Start(&g_timx_pwm_chy_handle, GTIM_TIMX_PWM_CH2);   
}



void GTIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == GTIM_TIMX_PWM)
    {
        GPIO_InitTypeDef gpio_init_struct;
        GTIM_TIMX_PWM_CHY_GPIO_CLK_ENABLE();                            
        GTIM_TIMX_PWM_CHY_CLK_ENABLE();                              

        gpio_init_struct.Pin = GTIM_TIMX_PWM_CHY_GPIO_PIN;             
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;                       
        gpio_init_struct.Pull = GPIO_PULLUP;                           
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;                 
        gpio_init_struct.Alternate = GTIM_TIMX_PWM_CHY_GPIO_AF;         
        HAL_GPIO_Init(GTIM_TIMX_PWM_CHY_GPIO_PORT, &gpio_init_struct);
    }
}

void motor_set_speed(int a)
{
    if(a >= 0 && a <= 300)
        __HAL_TIM_SET_COMPARE(&g_timx_pwm_chy_handle, GTIM_TIMX_PWM_CHY, a);
    else
        printf("error seppd set");
}


