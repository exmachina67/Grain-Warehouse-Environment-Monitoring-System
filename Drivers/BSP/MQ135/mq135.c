#include "./BSP/mq135/mq135.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/adc/adc.h"
#include "stdio.h"

// 添加SGP30初始化状态标志
static uint8_t sgp30_initialized = 0;
static uint32_t init_timestamp __attribute__((unused)) = 0;

// I2C延时函数
static void I2C_Delay(void)
{
    delay_us(4);  // 增加延时以适应SGP30
}

// 产生起始信号
static void I2C_Start(void)
{
    SDA_H();
    SCL_H();
    I2C_Delay();
    SDA_L();
    I2C_Delay();
    SCL_L();
}

// 产生停止信号
static void I2C_Stop(void)
{
    SDA_L();
    SCL_H();
    I2C_Delay();
    SDA_H();
    I2C_Delay();
}

// 等待应答
static uint8_t I2C_WaitAck(void)
{
    uint8_t ucErrTime = 0;
    
    SDA_H();
    I2C_Delay();
    SCL_H();
    I2C_Delay();
    
    while(SDA_READ())
    {
        ucErrTime++;
        if(ucErrTime > 250)
        {
            I2C_Stop();
            return 1;
        }
    }
    SCL_L();
    return 0;
}

// 产生应答
static void I2C_Ack(void)
{
    SCL_L();
    SDA_L();
    I2C_Delay();
    SCL_H();
    I2C_Delay();
    SCL_L();
}

// 不产生应答
static void I2C_NAck(void) __attribute__((unused));
static void I2C_NAck(void)
{
    SCL_L();
    SDA_H();
    I2C_Delay();
    SCL_H();
    I2C_Delay();
    SCL_L();
}

// 发送一个字节
static void I2C_SendByte(uint8_t txd)
{
    uint8_t i;
    
    SCL_L();
    for(i = 0; i < 8; i++)
    {
        if(txd & 0x80)
            SDA_H();
        else
            SDA_L();
        I2C_Delay();
        SCL_H();
        I2C_Delay();
        SCL_L();
        txd <<= 1;
    }
}

// 读取一个字节
static uint8_t I2C_ReadByte(void)
{
    uint8_t i, receive = 0;
    
    SDA_H();
    for(i = 0; i < 8; i++)
    {
        SCL_L();
        I2C_Delay();
        SCL_H();
        receive <<= 1;
        if(SDA_READ())
            receive++;
        I2C_Delay();
    }
    SCL_L();
    return receive;
}

// CRC8校验计算
static uint8_t SGP30_CRC8(uint8_t *data, uint8_t len) __attribute__((unused));
static uint8_t SGP30_CRC8(uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    uint8_t i, j;
    
    for(i = 0; i < len; i++) {
        crc ^= data[i];
        for(j = 0; j < 8; j++) {
            if(crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// 获取完整状态
SGP30_State MQ135_GetState(void)
{
    SGP30_State state = {0};
    state.co2_value = MQ135_ReadCO2();
    state.status = MQ135_GetStatus();
    state.initialized = sgp30_initialized;
    return state;
}

// 获取状态字符串
const char* MQ135_GetStatusString(void)
{
    uint8_t status = MQ135_GetStatus();
    switch(status) {
        case CO2_STATUS_NORMAL:
            return "Normal";
        case CO2_STATUS_DANGER:
            return "Danger";
        default:
            return "Unknown";
    }
}

// MQ135初始化函数实现
void MQ135_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t cmd[2];
    uint8_t i;
    
    // 使能GPIO时钟
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // 配置I2C引脚
    GPIO_InitStruct.Pin = MQ135_SCL_PIN | MQ135_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MQ135_SCL_GPIO, &GPIO_InitStruct);
    
    // 初始状态
    SCL_H();
    SDA_H();
    delay_ms(100);
    
    // 发送9个时钟脉冲
    for(i = 0; i < 9; i++) {
        SCL_L();
        I2C_Delay();
        SCL_H();
        I2C_Delay();
    }
    
    // 记录初始化时间戳
    init_timestamp = HAL_GetTick();
    
    // 发送初始化命令
    I2C_Start();
    I2C_SendByte(SGP30_ADDR << 1);
    if(I2C_WaitAck() == 0) {
        cmd[0] = SGP30_INIT_CMD >> 8;
        cmd[1] = SGP30_INIT_CMD & 0xFF;
        I2C_SendByte(cmd[0]);
        I2C_WaitAck();
        I2C_SendByte(cmd[1]);
        I2C_WaitAck();
        
        I2C_Stop();
        delay_ms(300);
        
        // 设置初始化基线
        I2C_Start();
        I2C_SendByte(SGP30_ADDR << 1);
        if(I2C_WaitAck() == 0) {
            cmd[0] = 0x20;
            cmd[1] = 0x1E;
            I2C_SendByte(cmd[0]);
            I2C_WaitAck();
            I2C_SendByte(cmd[1]);
            I2C_WaitAck();
            
            I2C_SendByte(0x00);
            I2C_WaitAck();
            I2C_SendByte(0x01);
            I2C_WaitAck();
            
            I2C_Stop();
            sgp30_initialized = 1;
        }
    }
    
//    printf("SGP30 Initialization completed\r\n");
}

// 读取CO2值函数实现
uint16_t MQ135_ReadCO2(void)
{
    uint8_t data[6];
    uint16_t co2_eq;
    uint8_t cmd[2];
    static uint16_t last_valid = 400;
    static uint8_t retry_count = 0;
    
//    // 检查预热
//    uint32_t current_time = HAL_GetTick();
//    if(current_time - init_timestamp < 15000) {
////        printf("SGP30 warming up... %d seconds remaining\r\n", 
////               (15000 - (current_time - init_timestamp)) / 1000);
//        return last_valid;
//    }
//    
//    // 重新初始化传感器
//    if (retry_count > 5) {
////        printf("Too many errors, reinitializing sensor...\r\n");
//        MQ135_Init();
//        retry_count = 0;
//        delay_ms(100);
//    }
    
    // 发送测量命令
    I2C_Start();
    I2C_SendByte(SGP30_ADDR << 1);
    if (I2C_WaitAck()) {
//        printf("Error: No ACK on write address\r\n");
        retry_count++;
        goto READ_ERROR;
    }
    
    cmd[0] = SGP30_MEASURE_CMD >> 8;
    cmd[1] = SGP30_MEASURE_CMD & 0xFF;
    I2C_SendByte(cmd[0]);
    if (I2C_WaitAck()) {
//        printf("Error: No ACK on command byte 1\r\n");
        retry_count++;
        goto READ_ERROR;
    }
    I2C_SendByte(cmd[1]);
    if (I2C_WaitAck()) {
//        printf("Error: No ACK on command byte 2\r\n");
        retry_count++;
        goto READ_ERROR;
    }
    
    I2C_Stop();
    delay_ms(30);
    
    // 读取测量结果
    I2C_Start();
    I2C_SendByte((SGP30_ADDR << 1) | 0x01);
    if (I2C_WaitAck()) {
//        printf("Error: No ACK on read address\r\n");
        retry_count++;
        goto READ_ERROR;
    }
    
    data[0] = I2C_ReadByte();
    I2C_Ack();
    data[1] = I2C_ReadByte();
    I2C_Ack();
    data[2] = I2C_ReadByte();
    I2C_Ack();
    
//    printf("Raw data: %02X %02X %02X\r\n", data[0], data[1], data[2]);
    
    co2_eq = ((uint16_t)data[0] << 8) | data[1];
    
    if (co2_eq >= 400 && co2_eq <= 60000) {  // SGP30的实际范围
        last_valid = co2_eq;
        retry_count = 0;
//        printf("Valid CO2: %d ppm\r\n", co2_eq);
        return co2_eq;
    }
    
READ_ERROR:
//    printf("Read Error or invalid value (%d), using last valid: %d ppm\r\n", co2_eq, last_valid);
    return last_valid;
}

// 获取状态
uint8_t MQ135_GetStatus(void)
{
    uint16_t co2_value = MQ135_ReadCO2();
    
//    printf("CO2 Status Check - Value: %d ppm, Threshold: %d ppm\r\n", co2_value, CO2_THRESHOLD);
    
    if (co2_value > CO2_THRESHOLD) {
//        printf("CO2 Status: DANGER\r\n");
        return CO2_STATUS_DANGER;
    }
//    printf("CO2 Status: NORMAL\r\n");
    return CO2_STATUS_NORMAL;
}
