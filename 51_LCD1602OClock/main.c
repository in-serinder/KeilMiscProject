#include <LCD1602.h>
#include <Timer.h>
#include <REGX52.H>
#include <DS1302.h>

sbit LCD_BALPOWER = P2 ^ 1;

sbit LCD_WAKEBUT = P3 ^ 2;
sbit COUNT_START = P3 ^ 3;

bit LCD_BALPOWER_FLAG = 1;
bit timeCount_flag = 0;

unsigned char BAL_KEEP = 60; // 秒

int secondCount = 0;

int main()
{
    LCD_Init();

    while (0)
    {
        BAL_KEEP--;

        LCD_ShowString(16, 1, getTimerString());
        // LCD_ShowString();
        // 背光电源
        if (BAL_KEEP == 0)
        {
            LCD_BALPOWER = 0; // 关闭背光
            LCD_BALPOWER_FLAG = 0;
        }
        else
        {
            LCD_BALPOWER = 1; // 打开背光
            LCD_BALPOWER_FLAG = 1;
        }
        // 计时器

        if (timeCount_flag)
        {
            secondCount++;
        }

        /*Delay*/
        Delay_ms(1000);
    }
}

// 功能按键使用外部中断

void LCDWakeBut_ISR(void) interrupt 0
{
    LCD_WAKEBUT = 1; // 清除中断标志
    BAL_KEEP = 60;
}

void countSTART(void) interrupt 1
{
    timeCount_flag = !timeCount_flag;
}