#include <Timer.h>
#include <INTRINS.H>

void Delay_us(unsigned int t)
{
    while (t--)
        ;
}

void Delay1000us(void) //@11.0592MHz
{
    unsigned char data i, j;

    _nop_();
    i = 2;
    j = 199;
    do
    {
        while (--j)
            ;
    } while (--i);
}

void Delay1000ms(void) //@11.0592MHz
{
    unsigned char data i, j, k;

    _nop_();
    i = 8;
    j = 1;
    k = 243;
    do
    {
        do
        {
            while (--k)
                ;
        } while (--j);
    } while (--i);
}

void Timer0_Init(void)
{
    TMOD |= 0x01; // 设置定时器0为模式1
    TH0 = 0xFC;   // 定时初值，1ms@11.0592MHz
    TL0 = 0x66;
    ET0 = 1; // 使能定时器中断
    TR0 = 0; // 先不启动定时器
}

void Delay1000ms1(void)
{
    unsigned int i;
    Timer0_Init();

    for (i = 0; i < 1000; i++) // 循环1000次，每次1ms
    {
        TR0 = 1; // 启动定时器
        while (!TF0)
            ;       // 等待溢出标志
        TF0 = 0;    // 清除溢出标志
        TH0 = 0xFC; // 重新加载初值
        TL0 = 0x66;
    }
    TR0 = 0; // 关闭定时器
}

void Delay_ms(unsigned int i) //@12.000MHz
{
    while (i)
    {
        Delay1000us();
        i--;
    }
}

void Delay_s(unsigned int i) //@12.000MHz
{
    while (i)
    {
        Delay1000ms();
        i--;
    }
}