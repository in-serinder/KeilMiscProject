#include "board.h"
/* uart.c — 串口1 驱动 @22.1184MHz
 *
 * 波特率：定时器1，12T 模式，16 位自动重装，重装值 0xFFFC
 *         baud = FOSC / 12 / 4 / (65536 - 0xFFFC) = 22118400/192 = 115200（误差 0%）
 * 接收：RI 中断把字节交给协议层组帧（Proto_OnByte）
 * 发送：查询 TI，帧长最大 51 字节，阻塞约 4.4ms，协议本身是"命令-应答"式，可接受
 *
 * 注意：TI 由发送函数自己轮询清零，中断服务里**不要**动 TI，
 *       否则会和查询等待产生竞争导致卡死。
 */
#include "uart.h"
#include "proto.h"

void Uart1_Init(void)
{
    SCON = 0x50;        /* 8 位数据，可变波特率，REN=1 允许接收 */
    AUXR &= 0x3F;       /* T0x12=0 / T1x12=0 -> 两个定时器都走 12T
                         * （注意 T1x12 是 bit7，不是 bit6；bit6 是 T0x12）*/
    AUXR &= 0xFE;       /* S1ST2=0：串口1 选择定时器1 为波特率发生器 */
    TMOD &= 0x0F;       /* 定时器1 -> 模式0（STC8：16 位自动重装载） */
    TL1 = 0xFC;
    TH1 = 0xFF;
    ET1 = 0;            /* 禁止定时器1中断 */
    TR1 = 1;            /* 启动定时器1 */
    TI = 0;
    RI = 0;
    ES = 1;             /* 允许串口1中断（总中断 EA 由 main 打开） */
}

void Uart1_SendByte(uint8_t dat)
{
    TI = 0;
    SBUF = dat;
    while (!TI);
    TI = 0;
}

void Uart1_SendStr(const char *s)
{
    while (*s != '\0')
    {
        Uart1_SendByte((uint8_t)(*s));
        s++;
    }
}

void Uart1_SendHex(uint8_t b)
{
    const char code HEX[17] = "0123456789ABCDEF";

    Uart1_SendByte((uint8_t)HEX[(b >> 4) & 0x0F]);
    Uart1_SendByte((uint8_t)HEX[b & 0x0F]);
}

/* 串口1 中断：向量 4 */
void Uart1_Isr(void) interrupt 4
{
    uint8_t dat;

    if (RI)
    {
        RI = 0;
        dat = SBUF;
        Proto_OnByte(dat);
    }
}
