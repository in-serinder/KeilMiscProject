#include "Display.h"

void Display_Init(void) {
  MAX7219_Init();
  TM1637_Init();
}
void Display_Clear(void) {
  MAX7219_Clear();
  TM1637_Clear();
  LED_RED_Off();
  LED_GREEN_Off();
}

void Display_Channel(uint8_t channel) {
  Display_Clear();

  if (channel >= 100) {
    TM1637_DisplayRaw(1, 0x39);
    TM1637_Display(2, channel / 100, 0);
    TM1637_Display(3, (channel / 10) % 10, 0);
    TM1637_Display(4, channel % 10, 0);
  } else {
    if (channel < 10) {
      TM1637_ClearPos(3); // 熄灭
    }
    TM1637_DisplayRaw(1, 0x39);
    TM1637_DisplayRaw(2, 0x76 | 0x80);
    TM1637_Display(3, channel / 10, 0);
    TM1637_Display(4, channel % 10, 0);
  }
}