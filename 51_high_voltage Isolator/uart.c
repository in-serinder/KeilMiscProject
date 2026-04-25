#include "uart.h"



// 串口初始化
void UART_Init(void)
{
    SCON = 0x50;  // 8位数据，可变波特率
    TMOD &= 0x0F; // 清除定时器1模式位
    TMOD |= 0x20; // 设定定时器1为8位自动重装方式
    TL1 = 0xFD;   // 设定定时初值，9600波特率
    TH1 = 0xFD;   // 设定定时器重装值
    ET1 = 0;      // 禁止定时器1中断
    TR1 = 1;      // 启动定时器1
}

// 发送一个字节
void UART_SendByte(unsigned char byte)
{
    SBUF = byte;
    while (!TI)
        ;
    TI = 0;
}

// 发送字符串
void UART_SendString(unsigned char *str)
{
    while (*str)
    {
        UART_SendByte(*str++);
    }
}

// 发送十六进制数
void UART_SendHex(unsigned char byte)
{
    unsigned char high, low;
    high = byte >> 4;
    low = byte & 0x0F;

    // 发送高位
    if (high < 10)
        UART_SendByte('0' + high);
    else
        UART_SendByte('A' + (high - 10));

    // 发送低位
    if (low < 10)
        UART_SendByte('0' + low);
    else
        UART_SendByte('A' + (low - 10));
}