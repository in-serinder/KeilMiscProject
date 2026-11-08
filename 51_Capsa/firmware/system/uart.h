#ifndef __UART_H__
#define __UART_H__

#include "board.h"

/* 串口1：115200 8N1，接收走中断 -> Proto_OnByte()，发送为查询等待 */
void Uart1_Init(void);
void Uart1_SendByte(uint8_t dat);
void Uart1_SendStr(const char *s);
void Uart1_SendHex(uint8_t b);

#endif /* __UART_H__ */
