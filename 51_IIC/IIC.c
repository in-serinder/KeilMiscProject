#include "IIC.h"

unsigned char Address = 0x3A;

void IIC_Start(void)
{
    SCL = 1;
    SDA = 1;
    Delay_us(10);
    SDA = 0; // 启动信号
    Delay_us(10);
    SCL = 0; // 钳住I2C总线，准备发送或接收数据
}

void IIC_Stop(void)
{
    SCL = 0;
    SDA = 1;
    Delay_us(10);
    SDA = 1;
    Delay_us(10);
}

/*
    基础函数
*/

void WriteByte(unsigned char Byte)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        SCL = 0;
        Delay_us(10);
        SDA = Byte & 0x80 ? 1 : 0; // 高位先出
        Delay_us(10);
        Byte <<= 1;
        SCL = 1;
        Delay_us(10);
    }
    while (SDA != 0)
        ; // 等待拉低SDA
}

void ReadByte(unsigned char *Byte)
{
    unsigned char i;
    SDA = 1; // 释放总线给主机
    for (i = 0; i < 8; i++)
    {
        SCL = 1;
        Delay_us(10);
        if (SDA)
        {
            Byte |= (0x80 >> i); // 高位写入
        }

        SCL = 0;
        Delay_us(10);
    }
}

/*
    应用函数
*/

void IIC_Init(void)
{
    SCL = 1;
    SDA = 1;
}

void IIC_Send(unsigned char *Byte)
{
    IIC_Start();
    WriteByte(Address);
    for (i = 0; i < sizeof(Byte) / sizeof(Byte[0]); i++)
    {

        WriteByte(Byte[i]);
    }

    // WriteByte(Byte);
    IIC_Stop();
}