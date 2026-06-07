

#include "stc89c52.h"

#ifndef __UART_H__
#define __UART_H__

void UART_Init(void);
void UART_SendByte(unsigned char byte);
void UART_SendString(unsigned char *str);
void UART_SendHex(unsigned char byte);
void UART_SendFloat(float value, unsigned char decimals);
void UART_SendInt(unsigned int value);

#endif // __UART_H__