#include "MAX7219.h"
#include "Delay.h"

#define MAX7219_CLK_PORT GPIOB
#define MAX7219_CLK_PIN GPIO_Pin_7
#define MAX7219_DIN_PORT GPIOB
#define MAX7219_DIN_PIN GPIO_Pin_9
#define MAX7219_CS_PORT GPIOB
#define MAX7219_CS_PIN GPIO_Pin_8

#define MAX7219_CLK_H() GPIO_SetBits(MAX7219_CLK_PORT, MAX7219_CLK_PIN)
#define MAX7219_CLK_L() GPIO_ResetBits(MAX7219_CLK_PORT, MAX7219_CLK_PIN)
#define MAX7219_DIN_H() GPIO_SetBits(MAX7219_DIN_PORT, MAX7219_DIN_PIN)
#define MAX7219_DIN_L() GPIO_ResetBits(MAX7219_DIN_PORT, MAX7219_DIN_PIN)
#define MAX7219_CS_H() GPIO_SetBits(MAX7219_CS_PORT, MAX7219_CS_PIN)
#define MAX7219_CS_L() GPIO_ResetBits(MAX7219_CS_PORT, MAX7219_CS_PIN)

static void MAX7219_SendByte(uint8_t dat) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    MAX7219_CLK_L();
    if (dat & 0x80)
      MAX7219_DIN_H();
    else
      MAX7219_DIN_L();
    dat <<= 1;
    MAX7219_CLK_H();
  }
}

void MAX7219_Write(uint8_t address, uint8_t dat) {
  MAX7219_CS_L();
  MAX7219_SendByte(address);
  MAX7219_SendByte(dat);
  MAX7219_CS_H();
  Delay_us(1);
}

void MAX7219_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

  GPIO_InitStructure.GPIO_Pin =
      MAX7219_CLK_PIN | MAX7219_DIN_PIN | MAX7219_CS_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOB, &GPIO_InitStructure);

  MAX7219_CLK_L();
  MAX7219_DIN_L();
  MAX7219_CS_H();
  Delay_ms(10);

  MAX7219_Write(MAX7219_SHUTDOWN, 0x01);
  MAX7219_Write(MAX7219_DISPLAYTEST, 0x00);
  MAX7219_Write(MAX7219_SCANLIMIT, 0x07);
  MAX7219_Write(MAX7219_DECODEMODE, 0xFF);
  MAX7219_Write(MAX7219_INTENSITY, 0x08);

  MAX7219_Clear();
}

void MAX7219_Display(uint8_t digit, uint8_t value, uint8_t dot_v) {
  MAX7219_Write(digit, value | (dot_v ? 0x80 : 0x00));
}

void MAX7219_Clear(void) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    MAX7219_Write(i + 1, 0x00);
  }
}

void MAX7219_ClearPos(uint8_t pos) { MAX7219_Write(pos, 0x00); }