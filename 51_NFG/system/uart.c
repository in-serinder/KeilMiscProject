#include "uart.h"

// 串口初始化: UART1, 115200bps @ 22.061MHz 晶振, 12T(单倍速)模式
//   STC90C58RD+ 实测为 12T 模式, 经典 8051 波特率发生器用定时器1(模式2)
//   + SMOD=1(波特率加倍):
//       Baud = 2^SMOD / 32 * SYSclk / (12 * (256 - TH1))
//   令 256 - TH1 = 1  (TH1 = 0xFF):
//       Baud = 2 * 22061000 / (32 * 12 * 1) = 114901  (误差 -0.26%, 可用)
//   注: 22.061MHz 在 12T 下无法精确得到 115200, 这是最接近的档位。
void UART_Init(void) // 约 115200bps@22.061MHz(12T), 实测约 114901
{
  SCON  = 0x50;      // 8位数据, 可变波特率, 模式1
  PCON |= 0x80;      // SMOD = 1 (波特率加倍) —— 关键
  TMOD &= 0x0F;      // 清 Timer1 控制位 (保留 Timer0)
  TMOD |= 0x20;      // 定时器1 模式2 (8位自动重装)
  TH1 = 0xFF;        // 重装值 (256-1=255)
  TL1 = 0xFF;        // 初值
  ET1 = 0;           // 禁止 Timer1 中断
  TR1 = 1;           // 启动 Timer1
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