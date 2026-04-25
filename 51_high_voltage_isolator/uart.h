#ifndef __UART_H__
#define __UART_H__

#include "stc15w.h"

void UART_Init(void);
void UART_SendByte(unsigned char byte);
void UART_SendString(unsigned char *str);
void UART_SendHex(unsigned char byte);

#endif // __UART_H__