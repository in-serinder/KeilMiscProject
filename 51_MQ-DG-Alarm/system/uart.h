#ifndef __UART_H__
#define __UART_H__

#include "board.h"

/* 串口1：P3.0=RXD  P3.1=TXD   9600 8N1
 *
 * 只做发送（调试输出用）：轮询 TI，不接中断。
 * 波特率：定时器1 12T 模式，16 位自动重装
 *         baud = FOSC / 12 / 4 / (65536 - 0xFFD0) = 22118400/2304 = 9600（误差 0%）
 *
 * ★ 定时器1 归串口做波特率，1ms 节拍用的是定时器0，两者互不影响；
 *   但 TMOD 只能改高 4 位（&= 0x0F），否则会把 T0 的模式冲掉。
 */
#if CFG_DEBUG_UART
void Uart1_Init(void);

void Uart1_SendByte(uint8_t dat);
void Uart1_SendStr(const char code *s);

/* 两位 16 进制（大写） */
void Uart1_SendHex8(uint8_t b);

/* 十进制，不补前导零 */
void Uart1_SendDec(uint16_t v);

void Uart1_SendCRLF(void);
#endif /* CFG_DEBUG_UART */

#endif /* __UART_H__ */
