#include "iic.h"

// 延时函数
void I2C_Delay(void)
{
    // unsigned char i = 2;
    unsigned char i = 100; // 低于100延时在解码中会出现多1字符解码问题 或许加个电阻同步阻抗会很好
    while (i--)
        ;
}

// 初始化I2C总线
void I2C_Init(void)
{
    SDA = 1;
    SCL = 1;
}

// 发送起始信号
void I2C_Start(void)
{
    SDA = 1;
    SCL = 1;
    I2C_Delay();
    SDA = 0;
    I2C_Delay();
    SCL = 0;
}

// 发送停止信号
void I2C_Stop(void)
{
    SDA = 0;
    SCL = 1;
    I2C_Delay();
    SDA = 1;
    I2C_Delay();
}

// 发送一个字节
void I2C_SendByte(unsigned char byte)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        SDA = byte & 0x80;
        byte <<= 1;
        SCL = 1;
        I2C_Delay();
        SCL = 0;
        I2C_Delay();
    }
}

// 接收一个字节
unsigned char I2C_ReceiveByte(void)
{
    unsigned char i, byte = 0;
    SDA = 1;
    for (i = 0; i < 8; i++)
    {
        byte <<= 1;
        SCL = 1;
        I2C_Delay();
        byte |= SDA;
        SCL = 0;
        I2C_Delay();
    }
    return byte;
}

// 等待应答
bit I2C_WaitAck(void)
{
    bit ack;
    SDA = 1;
    I2C_Delay();
    SCL = 1;
    I2C_Delay();
    ack = SDA;
    SCL = 0;
    I2C_Delay();
    return ack;
}

// 发送应答
void I2C_SendAck(bit ack)
{
    SDA = ack;
    I2C_Delay();
    SCL = 1;
    I2C_Delay();
    SCL = 0;
    I2C_Delay();
}

// 向EEPROM写入一个字节
void EEPROM_WriteByte(unsigned int addr, unsigned char dat)
{
    unsigned char wait_i = 255;
    I2C_Start();
    I2C_SendByte(EEPROM_ADDR);
    if (I2C_WaitAck())
        return;
    I2C_SendByte(addr >> 8);
    if (I2C_WaitAck())
        return;
    I2C_SendByte(addr & 0xFF);
    if (I2C_WaitAck())
        return;
    I2C_SendByte(dat);
    if (I2C_WaitAck())
        return;
    I2C_Stop();
    // 等待写入完成

    while (wait_i--)
        ;
    ;
}

// 从EEPROM读取一个字节
unsigned char EEPROM_ReadByte(unsigned int addr)
{
    unsigned char dat;
    I2C_Start();
    I2C_SendByte(EEPROM_ADDR);
    if (I2C_WaitAck())
        return 0;
    I2C_SendByte(addr >> 8);
    if (I2C_WaitAck())
        return 0;
    I2C_SendByte(addr & 0xFF);
    if (I2C_WaitAck())
        return 0;
    I2C_Start();
    I2C_SendByte(EEPROM_ADDR | 0x01);
    if (I2C_WaitAck())
        return 0;
    dat = I2C_ReceiveByte();
    I2C_SendAck(1);
    I2C_Stop();
    return dat;
}

// 向EEPROM写入字符串
void EEPROM_WriteString(unsigned int addr, unsigned char *str)
{
    while (*str)
    {
        EEPROM_WriteByte(addr++, *str++);
    }
    EEPROM_WriteByte(addr, 0); // 写入结束符
}

// 从EEPROM读取字符串
void EEPROM_ReadString(unsigned int addr, unsigned char *str, unsigned int len)
{
    unsigned int read_str_i;
    for (read_str_i = 0; read_str_i < len; read_str_i++)
    {
        *str = EEPROM_ReadByte(addr++);
        if (*str == 0)
            break;
        str++;
    }
    *str = 0; // 确保字符串结束
}

// 读取EEPROM指定长度内容
void EEPROM_ReadBuffer(unsigned int addr, unsigned char *buffer, unsigned int len)
{
    unsigned int read_i;
    for (read_i = 0; read_i < len; read_i++)
    {
        buffer[read_i] = EEPROM_ReadByte(addr + read_i);
    }
}

// 清空EEPROM全部内容
void EEPROM_ClearAll(void)
{
    unsigned int clear_i;
    for (clear_i = 0; clear_i < 8192; clear_i++) // 24C64容量为8KB
    {
        EEPROM_WriteByte(clear_i, 0);
    }
}