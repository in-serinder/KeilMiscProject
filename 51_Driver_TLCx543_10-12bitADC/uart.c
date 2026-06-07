#include "uart.h"
#include "math.h"

// 串口初始化
void UART_Init(void) {
  SCON = 0x50;  // 8位数据，可变波特率
  TMOD &= 0x0F; // 清除定时器1模式位
  TMOD |= 0x20; // 设定定时器1为8位自动重装方式
  TL1 = 0xFD;   // 设定定时初值，9600波特率
  TH1 = 0xFD;   // 设定定时器重装值
  ET1 = 0;      // 禁止定时器1中断
  TR1 = 1;      // 启动定时器1
}

// 发送一个字节
void UART_SendByte(unsigned char byte) {
  SBUF = byte;
  while (!TI)
    ;
  TI = 0;
}

// 发送字符串
void UART_SendString(unsigned char *str) {
  while (*str) {
    UART_SendByte(*str++);
  }
}

// 发送十六进制数
void UART_SendHex(unsigned char byte) {
  unsigned char idata high, low;
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

// 发送整数
void UART_SendInt(unsigned int value) {
  unsigned char idata buffer[6];
  unsigned char  idata i = 0;
  unsigned char  idata j;

  if (value == 0) {
    UART_SendByte('0');
    return;
  }

  while (value > 0) {
    buffer[i++] = value % 10 + '0';
    value /= 10;
  }

  for (j = i; j > 0; j--) {
    UART_SendByte(buffer[j - 1]);
  }
}

// 发送浮点数
void UART_SendFloat(float value, unsigned char decimals) {
  unsigned int idata integer_part;
  unsigned int  idata decimal_part;
  unsigned char idata i;

  // 处理负数
  if (value < 0) {
    UART_SendByte('-');
    value = -value;
  }

  // 提取整数部分
  integer_part = (unsigned int)value;

  // 提取小数部分
  decimal_part =
      (unsigned int)((value - integer_part) * pow(10, decimals) + 0.5);

  // 发送整数部分
  UART_SendInt(integer_part);

  // 发送小数点
  UART_SendByte('.');

  // 发送小数部分
  for (i = 0; i < decimals; i++) {
    unsigned int idata divisor = pow(10, decimals - i - 1);
    UART_SendByte((decimal_part / divisor) % 10 + '0');
  }
}