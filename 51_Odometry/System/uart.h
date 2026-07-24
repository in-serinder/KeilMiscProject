#ifndef __UART_H__
#define __UART_H__

#include "Delay.h"
#include "stc89c52.h"

void UART_Init(void);
void UART_SendByte(uint8_t byte);
void UART_SendString(uint8_t *str);
void UART_SendHex(uint8_t byte);

#endif // __UART_H__