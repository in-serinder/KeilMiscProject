// f:\SYSFILE\DeskTop\Misc.Git\KeilMiscProject\51_Rexhaustive_fakeload\System\UART.h
#ifndef __UART_H_
#define __UART_H_

#include "stc89c52.h"

void Uart1_Init(void);
void UART_SendChar(unsigned char ch);
void UART_SendString(unsigned char *str);

#endif