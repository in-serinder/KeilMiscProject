// #include <REG52.H>
#include <Max7219.h>
#include <Timer.h>
#include <MATH.H>

sbit MAX7219_CLK = P2 ^ 0;
sbit MAX7219_DIN = P2 ^ 1;
sbit MAX7219_LOAD = P2 ^ 2; // 加载信号（片选）

// 通过7219内部寄存器控制LED
// Load处于低电平有效状态
// 先将LOAD置高电平，再将数据写入7219，最后将LOAD置低电平 片选信号

// 修正：删除函数声明，直接定义函数

void Max7219_SendByte(unsigned char dat)
{
    unsigned char i;
    for (i = 0; i < 8; i++) // 每个片选（管位）寄存器（每位一个8位二进制的数据）
    {
        MAX7219_CLK = 0;                    // 先将CLK置低电平
        MAX7219_DIN = (dat & 0x80) ? 1 : 0; // 先发送高位
        dat <<= 1;                          // 左移一位，将下一位放到最高位
        MAX7219_CLK = 1;                    // 再将CLK置高电平,一位数据发送完成
        Delay_us(1);                        // 延时，等待数据稳定
    }
}

void Max7219_Write(unsigned char address, unsigned char dat)
{
    MAX7219_LOAD = 0;
    Max7219_SendByte(address);
    Max7219_SendByte(dat);
    MAX7219_LOAD = 1; // 完成传输
    Delay_us(1);
}

void Max7219_Init()
{

    // 使用正确的函数名和参数
    Max7219_Write(0x0C, 0x01); // 退出休眠模式
    Max7219_Write(0x0F, 0x00); // 关闭测试模式
    Max7219_Write(0x0B, 0x07); // 设置扫描界限为8位（DIG0~DIG7）
    Max7219_Write(0x09, 0xFF); // 启用BCD译码（简化数字显示）
    Max7219_Write(0x0A, 0x08); // 设置亮度（0~15，8为中等亮度）
}

void Max7219_Display(unsigned char digit, unsigned char value, unsigned char dot_v)
{
    Max7219_Write(digit, value | (dot_v ? 0x80 : 0x00));
}

void Max7219_Clear()
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        Max7219_Write(i + 1, 0x00);
    }
}

void Max7219_NumDisplay(double num)
{
    unsigned char i = 0, intCount = 0, floatCount = 0;
    float Float_Temp = num;
    int INT_pos, Float_pos;
    while (num /= 10) // 整数
    {
        INT_pos *= (int)num % 10;
        intCount++;
    }

    Float_pos -= INT_pos; // 小数
    floatCount -= intCount;

    for (i = 1; i <= intCount; i++)
    {
        Max7219_Display(i, INT_pos / pow(10, i - 1), i == intCount ? 0 : 1); // 显示整数部分
    }

    for (; i < 8; i++)
    {
        Max7219_Display(i, Float_pos / pow(10, (8 - i - 1)), 0); // 显示小数部分
    }
}
