#include "Channel_Ctl.h"
#include "Delay.h"
#include "LED.h"
#include "MAX7219.h"
#include "TM1637.h"
#include "stm32f10x.h"


void Test_MAX7219(void) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    MAX7219_Display(i + 1, (i + 1) % 10, 0);
    Delay_ms(200);
  }
  MAX7219_Display(1, 8, 1);
  MAX7219_Display(2, 8, 1);
  MAX7219_Display(3, 8, 1);
  MAX7219_Display(4, 8, 1);
  MAX7219_Display(5, 8, 1);
  MAX7219_Display(6, 8, 1);
  MAX7219_Display(7, 8, 1);
  MAX7219_Display(8, 8, 1);
  Delay_ms(1000);
  MAX7219_Clear();
  Delay_ms(500);
}

void Test_TM1637(void) {
  uint8_t i;
  for (i = 0; i < 10; i++) {
    TM1637_Display(1, i, 0);
    TM1637_Display(2, i, 0);
    TM1637_Display(3, i, 0);
    TM1637_Display(4, i, 0);
    Delay_ms(200);
  }
  TM1637_Display4Num(1234, 1);
  Delay_ms(1000);
  TM1637_Display4Num(5678, 0);
  Delay_ms(1000);
  TM1637_Clear();
  Delay_ms(500);
}

void Test_ChannelCtl(void) {
  ChannelSet_600V();
  Delay_ms(1000);
  ChannelSet_250V();
  Delay_ms(1000);
  ChannelSet_36V();
  Delay_ms(1000);
  ChannelSet_5V();
  Delay_ms(1000);
}

void Test_Combined(void) {
  uint16_t val = 0;
  LED_RED_On();
  LED_GREEN_Off();

  ChannelSet_600V();
  MAX7219_Display(1, 6, 0);
  MAX7219_Display(2, 0, 0);
  MAX7219_Display(3, 0, 1);
  MAX7219_Display(4, 0, 0);
  TM1637_Display4Num(6000, 1);
  Delay_ms(1500);
  LED_RED_Off();
  LED_GREEN_On();

  ChannelSet_250V();
  MAX7219_Clear();
  MAX7219_Display(1, 2, 0);
  MAX7219_Display(2, 5, 0);
  MAX7219_Display(3, 0, 1);
  MAX7219_Display(4, 0, 0);
  TM1637_Display4Num(2500, 1);
  Delay_ms(1500);
  LED_RED_Off();
  LED_GREEN_On();

  ChannelSet_36V();
  MAX7219_Clear();
  MAX7219_Display(1, 3, 0);
  MAX7219_Display(2, 6, 1);
  MAX7219_Display(3, 0, 0);
  MAX7219_Display(4, 0, 0);
  TM1637_Display4Num(0036, 0);
  Delay_ms(1500);
  LED_RED_Off();
  LED_GREEN_On();

  ChannelSet_5V();
  MAX7219_Clear();
  MAX7219_Display(1, 0, 0);
  MAX7219_Display(2, 5, 1);
  MAX7219_Display(3, 0, 0);
  MAX7219_Display(4, 0, 0);
  TM1637_Display4Num(0005, 0);
  Delay_ms(1500);
  LED_RED_Off();
  LED_GREEN_On();
  MAX7219_Clear();
  TM1637_Clear();
  for (val = 0; val < 10000; val += 1111) {
    TM1637_Display4Num(val, val % 2);
    MAX7219_Display(1, (val / 1000) % 10, 0);
    MAX7219_Display(2, (val / 100) % 10, 1);
    MAX7219_Display(3, (val / 10) % 10, 0);
    MAX7219_Display(4, val % 10, 1);
    Delay_ms(500);
  }
  MAX7219_Clear();
  TM1637_Clear();
}

int main(void) {
  ChannelSet_Init();
  MAX7219_Init();
  TM1637_Init();
  LED_Init();

  while (1) {
    Test_MAX7219();
    Test_TM1637();
    Test_ChannelCtl();
    Test_Combined();
  }
}