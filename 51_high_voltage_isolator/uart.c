#include "uart.h"

// 串口初始化
void UART_Init(void) {
  SCON = 0x50;  // 8位数据,可变波特率
  AUXR |= 0x40; // 定时器时钟1T模式
  AUXR &= 0xFE; // 串口1选择定时器1为波特率发生器
  TMOD &= 0x0F; // 设置定时器模式
  TL1 = 0xE0;   // 设置定时初始值
  TH1 = 0xFE;   // 设置定时初始值
  ET1 = 0;      // 禁止定时器中断
  TR1 = 1;      // 定时器1开始计时
}

// 发送一个字节
void UART_SendByte(uint8_t byte) {
  SBUF = byte;
  while (!TI)
    ;
  TI = 0;
}

// 发送字符串
void UART_SendString(uint8_t *str) {
  while (*str) {
    UART_SendByte(*str++);
  }
}

// 发送十六进制数
void UART_SendHex(uint8_t byte) {
  uint8_t high, low;
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