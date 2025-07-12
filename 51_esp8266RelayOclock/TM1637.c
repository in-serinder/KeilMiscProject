#include "TM1637.h"
#include <Timer.h>

sbit CLK = P3 ^ 4;
sbit DIO = P3 ^ 5;

// 数据方式
// 通过命令方式传输消息
// 上升沿切换方式决定指令发送 0x80显示关闭

unsigned char code SMG_duanma[18] = {
    // 或code存储
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, // 0-7
    0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, // 8-9,A-F
    0x40, 0x00                                      // -,熄灭
};

// 起始信号准备发送指令
void TM1637_Start()
{
    CLK = 1;
    DIO = 1; // 确保DIO为高电平
    Delay_us(2);
    DIO = 0; // DIO从高到低，产生起始信号
    Delay_us(2);
}

// 停止信号完成一次发送
void TM1637_Stop()
{
    CLK = 0;
    Delay_us(2);
    DIO = 0;
    Delay_us(2);
    CLK = 1; // CLK先拉高
    Delay_us(2);
    DIO = 1; // DIO再拉高，产生停止信号
    Delay_us(2);
}

// 向TM1637发送一个字节
void TM1637_WriteByte(unsigned char byte)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        CLK = 0;
        Delay_us(2);
        if (byte & 0x01)
        {
            DIO = 1;
        }
        else
        {
            DIO = 0;
        }
        Delay_us(2);
        byte >>= 1;
        CLK = 1;
        Delay_us(2);
    }
    // 等待数据移位完成
    CLK = 0;
    DIO = 1;
    Delay_us(2);
    CLK = 1;
    Delay_us(2);
    CLK = 0;
}

// 初始化TM1637
void TM1637_Init(void)
{
    CLK = 1;
    DIO = 1;

    TM1637_Start();
    TM1637_WriteByte(0x8f); // 亮度最大
    TM1637_Stop();

    TM1637_Start();
    TM1637_WriteByte(0x40); // 数据命令设置：地址自动加1 (自动地址增加模式)
    TM1637_Stop();

    // 清屏
    TM1637_Start();
    TM1637_WriteByte(0xc0); // 地址命令设置：起始地址0x00
    TM1637_WriteByte(0x00); // 第1位数据
    TM1637_WriteByte(0x00); // 第2位数据
    TM1637_WriteByte(0x00); // 第3位数据
    TM1637_WriteByte(0x00); // 第4位数据
    TM1637_Stop();
}

// 显示函数 - 兼容固定地址模式
void TM1637_Display(unsigned char pos, unsigned char num, bit colon)
{
    unsigned char seg_data;

    // 获取段码
    seg_data = SMG_duanma[num];

    // 如果需要显示冒号，将最高位置1
    if (colon)
        seg_data |= 0x80; // 0x80 = 1000 0000

    // 使用固定地址模式
    TM1637_Start();
    TM1637_WriteByte(0x44); // 固定地址模式
    TM1637_Stop();

    TM1637_Start();
    TM1637_WriteByte(0xc0 | (pos - 1)); // 设置地址
    TM1637_WriteByte(seg_data);         // 发送数据
    TM1637_Stop();
}

void TM1637_Display4Num(unsigned int num, bit colon)
{
    unsigned char xdata pos1, pos2, pos3, pos4;

    pos4 = num % 10;
    pos3 = num / 10 % 10;
    pos2 = num / 100 % 10;
    pos1 = num / 1000;

    // 当第一位为0时显示为空
    if (pos1 == 0)
    {
        TM1637_Display(1, 17, 0); // 17对应SMG_duanma中的熄灭状态
    }
    else
    {
        TM1637_Display(1, pos1, 0);
    }

    TM1637_Display(2, pos2, colon);
    TM1637_Display(3, pos3, 0);
    TM1637_Display(4, pos4, 0);
}
void TM1637_StringDisplay(char *str, bit colon)
{
    // unsigned char i;
    // for (i = 0; i < 4; i++)
    // {
    //     TM1637_Display(i + 1, (str[i] == '0' && i == 0) ? 0x00 : str[i], i == 1 && colon);
    // }
    TM1637_Display(1, charToint(str[0] == '0' ? 17 : str[0]), 0); // 使用上方索引
    TM1637_Display(2, charToint(str[1]), colon);
    TM1637_Display(3, charToint(str[2]), 0);
    TM1637_Display(4, charToint(str[3]), 0);

    // TM1637_Display(1, 3, 0); // 使用上方索引
    // TM1637_Display(2, 2, colon);
    // TM1637_Display(3, 1, 0);
    // TM1637_Display(4, 1, 0);
}