/**
 ****************************************************************************************************
 * @file        atk_tof.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2024-11-01
 * @brief       ATK_TOFģ�� ��������
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� M48Z-M3��Сϵͳ��STM32F103��
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "./BSP/ATK_TOF/atk_tof.h"
#include "./BSP/ATK_TOF/vi5300/VI530x_API.h"
#include "./BSP/ATK_TOF/vi5300/VI530x_Firmware.h"
#include "./BSP/ATK_TOF/vi5300/VI530x_System_Data.h"


/**
 * @brief       ATK-TOFģ��Ӳ����λ
 * @param       ��
 * @retval      ��
 */
void atk_tof_hw_reset(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    
    ATK_TOF_XSH_GPIO_CLK_ENABLE();
    
    /* ��ʼ��XS���� */
    gpio_init_struct.Pin    = ATK_TOF_XSH_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_TOF_XSH_GPIO_PORT, &gpio_init_struct);
        
    ATK_TOF_XSH(0);         /* ����XS���� */
    delay_ms(30);           /* ��ʱ */
    ATK_TOF_XSH(1);         /* ����XS���� */
    delay_ms(30);           /* ��ʱ */
}

/**
 * @brief       ATK-TOFģ��Ӳ����ʼ��
 * @param       ��
 * @retval      ��
 */
void atk_tof_hw_init(void)
{
    VI530x_Status ret = VI530x_OK;
    
    /* ��ʼ��IIC�ӿ� */
    iic_init();
    
    /* �����жϣ�GPIO����ֱ������ */
    VI530x_Cali_Data.VI530x_Interrupt_Mode_Status = 0x00;

    /* ��ʼ��VI5300 */
    ret |= VI530x_Chip_Init();

    /* д��VI5300�̼� */
    ret |= VI530x_Download_Firmware((uint8_t *)VI5300_M31_firmware_buff, FirmwareSize());

    /* ���ñ궨ֵ */
    ret |= VI530x_Set_Californiation_Data(VI530x_Cali_Data.VI530x_Calibration_Offset);

    /* �����¶�У׼, 0x00:�أ�0x01:�� */
    ret |= VI530x_Set_Sys_Temperature_Enable(0x01);
    
    /* ����֡�ʣ����ִ��� */
    ret |= VI530x_Set_Integralcounts_Frame(30, 131072);

    /* �������ݶ�ȡģʽ */
#if CONTINUOUS_READ == 1
    ret |= VI530x_Start_Continue_Ranging_Cmd(); /* ����ģʽ */
#else
    ret = VI530x_Start_Single_Ranging_Cmd();    /* ����ģʽ */
#endif

//    if(ret)                   // 调试用
//    {
//        printf("VI5300��ʼ��ʧ��!\r\n");
//    }
//    else
//    {
//        printf("VI5300��ʼ���ɹ�!\r\n");
//    }
}

/**
 * @brief   дN���ֽڵ�ATK-TOFģ��ļĴ���
 * @param   reg_addr:   �Ĵ�����ַ
 * @param   data:       д������
 * @param   len:        ���ݳ���
 * @retval  д����
 * @arg     0   : �ɹ�
 * @arg     ����: ʧ��
 */
VI530x_Status atk_tof_write_nbytes(uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    iic_start();                        /* ��ʼ�ź� */
    
    iic_send_byte(ATK_TOF_IIC_ADDR);    /* ����IICͨѶ��ַ */
    if (iic_wait_ack() == 1)            /* �ȴ�Ӧ�� */
    {
        iic_stop();
        return VI530x_IIC_ERROR;
    }
    
    iic_send_byte(reg_addr);            /* ���ͼĴ�����ַ */
    if (iic_wait_ack() == 1)            /* �ȴ�Ӧ�� */
    {
        iic_stop();
        return VI530x_IIC_ERROR;
    }
    
    for (uint16_t i = 0; i < len; i++)  /* ѭ���������� */
    {
        iic_send_byte(data[i]);
        if (iic_wait_ack() == 1)        /* �ȴ�Ӧ�� */
        {
            iic_stop();
            return VI530x_IIC_ERROR;
        }
    }
    
    iic_stop();                         /* ֹͣ�ź� */
    
    return VI530x_OK;
}

/**
 * @brief   дһ���ֽڵ�ATK-TOFģ��ļĴ���
 * @param   reg_addr:   �Ĵ�����ַ
 * @param   data:       ����
 * @retval  д����
 * @arg     0   : �ɹ�
 * @arg     ����: ʧ��
 */
VI530x_Status atk_tof_write_byte(uint8_t reg_addr, uint8_t data)
{
    uint8_t ret = 0;
    
    ret = atk_tof_write_nbytes(reg_addr, &data, 1);     /* д��һ���ֽ����� */
    
    if(ret != 0)
    {
        return VI530x_IIC_ERROR;
    }
    else
    {
        return VI530x_OK;
    }
}

/**
 * @brief   ��ATK-TOFģ���ȡN���ֽ�����
 * @param   reg_addr:   �Ĵ�����ַ
 * @param   data:       ���ݴ洢buff
 * @param   len:        ���ݳ���
 * @retval  �������
 * @arg     0   : �ɹ�
 * @arg     ����: ʧ��
 */
VI530x_Status atk_tof_read_nbytes(uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    iic_start();                        /* ��ʼ�ź� */
    
    iic_send_byte(ATK_TOF_IIC_ADDR);    /* ����IICͨѶ��ַ */
    if (iic_wait_ack() == 1)            /* �ȴ�Ӧ�� */
    {
        iic_stop();
        return VI530x_IIC_ERROR;
    }
    
    iic_send_byte(reg_addr);            /* ���ͼĴ�����ַ */
    if (iic_wait_ack() == 1)            /* �ȴ�Ӧ�� */
    {
        iic_stop();
        return VI530x_IIC_ERROR;
    }
    
    iic_start();                        /* ��ʼ�ź� */
    
    iic_send_byte(ATK_TOF_IIC_ADDR | 0x01);
    if (iic_wait_ack() == 1)            /* �ȴ�Ӧ�� */
    {
        iic_stop();
        return VI530x_IIC_ERROR;
    }
    
    while (len)                         /* ѭ���������� */
    {
        *data = iic_read_byte((len > 1) ? 1 : 0);
        len--;
        data++;
    }
    
    iic_stop();                         /* ֹͣ�ź� */
    
    return VI530x_OK;
}

/**
 * @brief   ��ATK-TOFģ���ȡ1���ֽ�����
 * @param   reg_addr: �Ĵ�����ַ
 * @param   data:     ���ݴ洢buff
 * @arg     0   : �ɹ�
 * @arg     ����: ʧ��
 */
VI530x_Status atk_tof_read_byte(uint8_t reg_addr, uint8_t *data)
{
    uint8_t ret = 0;
    
    ret = atk_tof_read_nbytes(reg_addr, data, 1);       /* ��ȡһ���ֽ����� */
    
    if(ret != 0)
    {
        return VI530x_IIC_ERROR;
    }
    else
    {
        return VI530x_OK;
    }
}



