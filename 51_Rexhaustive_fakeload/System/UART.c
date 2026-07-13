#include "UART.h"

void Uart1_Init(void) {
  SCON = 0x50;  // 模式1，8位UART，允许接收
  TMOD &= 0x0F; // 清空T1控制位
  TMOD |= 0x20; // T1模式2（8位自动重装）
  TH1 = 0xFD;   // 11.0592MHz 9600bps 重装值
  TL1 = 0xFD;
  TR1 = 1; // 启动定时器1
  TI = 1;  // ★★★ 关键！将TI置1，让第一次发送能顺利开始 ★★★
           // ES = 0;      // 若不用中断，可不开启
}

// 发送单个字符
void UART_SendChar(unsigned char ch) {
  SBUF = ch;
  while (!TI)
    ;     // 等待发送完成
  TI = 0; // 清除发送标志
}

// 发送字符串
void UART_SendString(unsigned char *str) {
  while (*str != '\0') {
    UART_SendChar(*str++);
  }
}