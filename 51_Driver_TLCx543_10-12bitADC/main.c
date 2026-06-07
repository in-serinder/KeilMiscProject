#include "TLCx543.h"
#include "uart.h"

#define CHANNEL_COUNT 11

void Delay_ms(uint16_t ms) {
  uint16_t i, j;
  for (i = 0; i < ms; i++)
    for (j = 0; j < 120; j++)
      ;
}

void main(void) {
  uint8_t channel;
  float voltage = 0.0f;

  UART_Init();
  TLCx543_Init(TLC2543);

  // 必须空读 2 流水线初始化
  TLCx543_ReadADC(0);
  Delay_ms(20);
  TLCx543_ReadADC(0);
  Delay_ms(20);

  while (1) {
    UART_SendString("\r\n--- Sample --\r\n");

    for (channel = 0; channel < 11; channel++) {
      voltage = TLCx543_ReadVoltageV(channel);

      UART_SendString("A");
      UART_SendInt(channel);
      UART_SendString(": ");
      UART_SendFloat(voltage, 4);
      UART_SendString("V\r\n");

      Delay_ms(10);
    }
    Delay_ms(500);
  }
}