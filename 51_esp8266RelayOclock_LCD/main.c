#include <Ser.h>
#include <Timer.h>
#include <stdio.h>
#include <Util.h>
#include <LCD1602.h>

/* ----串口消息格式----
 *时间戳: 2023-10-24-16:00:00^
 *继电器状态 000^
 *温湿度 00.00^00.00^
 *警告位 0^
 *日期更新位 0
 *完全格式 yyyy-mm-dd-tt:mm:ss^000^00.00^00.00^0^0
 */

/*
使用外部中断的IIC


*/

sbit Relay1 = P2 ^ 0;
sbit Relay2 = P2 ^ 1;
sbit Relay3 = P2 ^ 2;

sbit WarnLed = P1 ^ 0;

sbit Buzzer = P1 ^ 1;

sbit LCD_BLK = P1 ^ 2;
/*全局变量*/

unsigned char loopSpan = 0;
unsigned char time[7];
Util UData;

void main()
{

    ///////////////////////////////////////////
    // 初始化
    LCD_Init();
    UART_Init();
    DS1302_Init();

    InitExternalInterrupt0();

    Relay1 = 0;
    Relay2 = 0;
    Relay3 = 0;
    WarnLed = 0;
    BuzzerS1 = 0;

    //////////////////////////////////////////
    while (1)
    {
        // 10ms阶段刷新,1s重制
        Delay_ms(10);
        if (loopSpan++ >= 1000)
        {
            loopSpan = 0;
        }

        /*-------------功能块-------------*/

        /*-------------功能块-------------*/
    }
}