#include "UART.h"
#include "ADC.h"
#include <stdarg.h>
#include <stdio.h>

// ============================================================
// USART1: PA9(TX)  PA10(RX)  115200-8N1
// ============================================================

void UART_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

  // TX(PA9) = 推挽复用输出
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // RX(PA10) = 浮空输入
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  USART_InitStructure.USART_BaudRate = 115200;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl =
      USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
  USART_Init(USART1, &USART_InitStructure);

  USART_Cmd(USART1, ENABLE);
}

void UART_SendChar(char c) {
  while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    ;
  USART_SendData(USART1, (uint8_t)c);
}

void UART_SendString(const char *str) {
  while (*str) {
    if (*str == '\n')
      UART_SendChar('\r'); // 自动加回车
    UART_SendChar(*str);
    str++;
  }
}

void UART_Printf(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  UART_SendString(buf);
}

// ============================================================
// 打印所有5个ADC通道的电压值（用于串口调试）
// ============================================================
void UART_PrintAllADC(void) {
  double v800, v600, v240, v36, v5;

  v800 = ADC_Get800V();
  v600 = ADC_Get600V();
  v240 = ADC_Get240V();
  v36 = ADC_Get36V();
  v5 = ADC_Get5V();

  UART_Printf("800V:%.3fV | 600V:%.3fV | 240V:%.3fV | 36V:%.3fV | 5V:%.3fV\n",
              v800, v600, v240, v36, v5);
}

void UART_PrintAllADC_Head(void) {
  UART_SendString("\r\n====== ADC All Channels ======\r\n");
}