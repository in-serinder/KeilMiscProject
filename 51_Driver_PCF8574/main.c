#include "pcf8574.h"
#include "intrins.h"

// 延时函数 约100ms @11.0592MHz
void Delay100ms(void)
{
    unsigned char data i, j, k;

    _nop_();
    _nop_();
    i = 5;
    j = 52;
    k = 195;
    do
    {
        do
        {
            while (--k);
        } while (--j);
    } while (--i);
}

// 流水灯测试函数
void RunningLight(void)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        PCF8574_Write(~(1 << i));  // 低电平点亮LED
        Delay100ms();
    }
}

// 全闪测试函数
void AllFlash(void)
{
    PCF8574_Write(0x00);  // 全亮
    Delay100ms();
    PCF8574_Write(0xFF);  // 全灭
    Delay100ms();
}

// 交替闪烁测试函数
void AlternateFlash(void)
{
    PCF8574_Write(0xAA);  // 01010101
    Delay100ms();
    PCF8574_Write(0x55);  // 10101010
    Delay100ms();
}

void main(void)
{
    // 初始化PCF8574
    PCF8574_Init();
		P1 = 0x00;
    while (1)
    {
        // 测试1: 流水灯
        RunningLight();
        
        // 测试2: 全闪
       // AllFlash();
        //AllFlash();
        
        // 测试3: 交替闪烁
        //AlternateFlash();
        //AlternateFlash();
    }
}