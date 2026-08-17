
#ifndef __GTIM_H
#define __GTIM_H

#include "./SYSTEM/SYS/sys.h"


 
#define GTIM_TIMX_INT                       TIM5
#define GTIM_TIMX_INT_IRQn                  TIM5_IRQn
#define GTIM_TIMX_INT_IRQHandler            TIM5_IRQHandler
#define GTIM_TIMX_INT_CLK_ENABLE()          do{ __HAL_RCC_TIM5_CLK_ENABLE(); }while(0)   


//#define GTIM_TIMX_PWM_CHY_GPIO_PORT         GPIOC
//#define GTIM_TIMX_PWM_CHY_GPIO_PIN          GPIO_PIN_6
//#define GTIM_TIMX_PWM_CHY_GPIO_PIN7         GPIO_PIN_7
//#define GTIM_TIMX_PWM_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)  
//#define GTIM_TIMX_PWM_CHY_GPIO_AF           GPIO_AF2_TIM3 

#define GTIM_TIMX_PWM_CHY_GPIO_PORT         GPIOA
#define GTIM_TIMX_PWM_CHY_GPIO_PIN          GPIO_PIN_6
#define GTIM_TIMX_PWM_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)  
#define GTIM_TIMX_PWM_CHY_GPIO_AF           GPIO_AF2_TIM3   



#define GTIM_TIMX_PWM                       TIM3                                       
#define GTIM_TIMX_PWM_CHY                   TIM_CHANNEL_1     
#define GTIM_TIMX_PWM_CH2                   TIM_CHANNEL_2
#define GTIM_TIMX_PWM_CHY_CCRX              TIM3->CCR1                                  
#define GTIM_TIMX_PWM_CHY_CLK_ENABLE()      do{ __HAL_RCC_TIM3_CLK_ENABLE(); }while(0)  

/****************************************************************************************************/

void gtim_timx_int_init(uint16_t arr, uint16_t psc);        
void gtim_timx_pwm_chy_init(uint16_t arr, uint16_t psc);    
void GTIM_PWM_MspInit(TIM_HandleTypeDef *htim);      
void motor_set_speed(int a);

#endif

















