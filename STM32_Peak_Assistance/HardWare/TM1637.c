#include "TM1637.h"
#include "Delay.h"

#define TM1637_CLK_PORT GPIOB
#define TM1637_CLK_PIN GPIO_Pin_12
#define TM1637_DIO_PORT GPIOB
#define TM1637_DIO_PIN GPIO_Pin_13

#define TM1637_CLK_H() GPIO_SetBits(TM1637_CLK_PORT, TM1637_CLK_PIN)
#define TM1637_CLK_L() GPIO_ResetBits(TM1637_CLK_PORT, TM1637_CLK_PIN)
#define TM1637_DIO_H() GPIO_SetBits(TM1637_DIO_PORT, TM1637_DIO_PIN)
#define TM1637_DIO_L() GPIO_ResetBits(TM1637_DIO_PORT, TM1637_DIO_PIN)

static const uint8_t SMG_duanma[18] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d,
                                       0x7d, 0x07, 0x7f, 0x6f, 0x77, 0x7c,
                                       0x39, 0x5e, 0x79, 0x71, 0x40, 0x00};

static uint8_t TM1637_buf[4] = {0, 0, 0, 0};

static void TM1637_Start(void) {
  TM1637_CLK_H();
  TM1637_DIO_H();
  Delay_us(2);
  TM1637_DIO_L();
  Delay_us(2);
}

static void TM1637_Stop(void) {
  TM1637_CLK_L();
  Delay_us(2);
  TM1637_DIO_L();
  Delay_us(2);
  TM1637_CLK_H();
  Delay_us(2);
  TM1637_DIO_H();
  Delay_us(2);
}

static void TM1637_WriteByte(uint8_t byte) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    TM1637_CLK_L();
    Delay_us(2);
    if (byte & 0x01)
      TM1637_DIO_H();
    else
      TM1637_DIO_L();
    Delay_us(2);
    byte >>= 1;
    TM1637_CLK_H();
    Delay_us(2);
  }
  TM1637_CLK_L();
  TM1637_DIO_H();
  Delay_us(2);
  TM1637_CLK_H();
  Delay_us(2);
  TM1637_CLK_L();
}

void TM1637_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

  GPIO_InitStructure.GPIO_Pin = TM1637_CLK_PIN | TM1637_DIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOB, &GPIO_InitStructure);

  TM1637_CLK_H();
  TM1637_DIO_H();

  TM1637_Start();
  TM1637_WriteByte(0x8f);
  TM1637_Stop();

  TM1637_Start();
  TM1637_WriteByte(0x40);
  TM1637_Stop();

  TM1637_Clear();
}

void TM1637_Display(uint8_t pos, uint8_t num, uint8_t colon) {
  uint8_t seg_data;

  if (num >= 18)
    seg_data = 0x00;
  else
    seg_data = SMG_duanma[num];

  if (colon)
    seg_data |= 0x80;

  TM1637_DisplayRaw(pos, seg_data);
}

void TM1637_Display4Num(uint16_t num, uint8_t colon) {
  uint8_t d1, d2, d3, d4;

  d4 = num % 10;
  d3 = num / 10 % 10;
  d2 = num / 100 % 10;
  d1 = num / 1000;

  TM1637_Display(1, d1, 0);
  TM1637_Display(2, d2, colon);
  TM1637_Display(3, d3, 0);
  TM1637_Display(4, d4, 0);
}

void TM1637_DisplayRaw(uint8_t pos, uint8_t seg_data) {
  if (pos >= 1 && pos <= 4)
    TM1637_buf[pos - 1] = seg_data;

  TM1637_Start();
  TM1637_WriteByte(0x44);
  TM1637_Stop();

  TM1637_Start();
  TM1637_WriteByte(0xc0 | (pos - 1));
  TM1637_WriteByte(seg_data);
  TM1637_Stop();
}

void TM1637_SetColon(uint8_t pos, uint8_t on) {
  uint8_t seg_data;
  if (pos < 1 || pos > 4)
    return;
  seg_data = TM1637_buf[pos - 1];
  if (on)
    seg_data |= 0x80;
  else
    seg_data &= 0x7F;
  TM1637_DisplayRaw(pos, seg_data);
}

void TM1637_ToggleColon(uint8_t pos) {
  uint8_t seg_data;
  if (pos < 1 || pos > 4)
    return;
  seg_data = TM1637_buf[pos - 1];
  seg_data ^= 0x80;
  TM1637_DisplayRaw(pos, seg_data);
}

void TM1637_Clear(void) {
  uint8_t i;
  TM1637_Start();
  TM1637_WriteByte(0x40);
  TM1637_Stop();

  TM1637_Start();
  TM1637_WriteByte(0xc0);
  for (i = 0; i < 4; i++)
    TM1637_WriteByte(0x00);
  TM1637_Stop();
}

void TM1637_ClearPos(uint8_t pos) { TM1637_DisplayRaw(pos, 0x00); }