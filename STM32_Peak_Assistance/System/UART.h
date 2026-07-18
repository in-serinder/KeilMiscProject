#ifndef __UART_H__
#define __UART_H__

#include "stm32f10x.h"

void UART_Init(void);
void UART_SendChar(char c);
void UART_SendString(const char *str);
void UART_Printf(const char *fmt, ...);
void UART_PrintAllADC(void);

#endif