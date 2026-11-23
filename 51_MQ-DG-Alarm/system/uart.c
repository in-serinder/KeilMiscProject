/* uart.c — 串口1 调试输出（9600 8N1 @22.1184MHz）
 *
 * 定时器1 作波特率发生器，12T + 16 位自动重装，重装值 0xFFD0：
 *   baud = 22118400 / 12 / 4 / (65536 - 0xFFD0) = 9600，误差 0%。
 *
 * 只发不收：本工程不需要接收，不开 ES，避免和 main 抢中断。
 */
#include "uart.h"

#if CFG_DEBUG_UART

void Uart1_Init(void)
{
    SCON = 0x50;        /* 8 位数据，可变波特率，REN=1（不收也无妨） */
    AUXR &= 0xBF;       /* 定时器1 时钟 12T 模式（bit7 = T0x12 必须保留） */
    AUXR &= 0xFE;       /* 串口1 选择定时器1 为波特率发生器 */
    TMOD &= 0x0F;       /* 定时器1 -> 模式0（STC8：16 位自动重装载） */
    TL1 = 0xD0;
    TH1 = 0xFF;
    ET1 = 0;            /* 禁止定时器1中断 */
    TR1 = 1;            /* 启动定时器1 */
    TI = 0;
    RI = 0;
}

void Uart1_SendByte(uint8_t dat)
{
    TI = 0;
    SBUF = dat;
    while (!TI);
    TI = 0;
}

void Uart1_SendStr(const char code *s)
{
    while (*s != '\0')
    {
        Uart1_SendByte((uint8_t)(*s));
        s++;
    }
}

void Uart1_SendHex8(uint8_t b)
{
    const char code HEX[17] = "0123456789ABCDEF";

    Uart1_SendByte((uint8_t)HEX[(b >> 4) & 0x0F]);
    Uart1_SendByte((uint8_t)HEX[b & 0x0F]);
}

void Uart1_SendDec(uint16_t v)
{
    uint8_t buf[5];
    uint8_t n = 0;

    if (v == 0)
    {
        Uart1_SendByte((uint8_t)'0');
        return;
    }

    while (v != 0)
    {
        buf[n] = (uint8_t)('0' + (uint8_t)(v % 10));
        v = (uint16_t)(v / 10);
        n++;
    }

    while (n != 0)
    {
        n--;
        Uart1_SendByte(buf[n]);
    }
}

void Uart1_SendCRLF(void)
{
    Uart1_SendByte(0x0D);
    Uart1_SendByte(0x0A);
}

#endif /* CFG_DEBUG_UART */
