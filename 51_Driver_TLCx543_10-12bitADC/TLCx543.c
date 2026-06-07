#include "TLCx543.h"
#include <intrins.h>

uint8_t idata TLC_CHIP_TYPE = TLC1543; // 默认10位ADC

void SPI_Delay(void) {
  _nop_();
  _nop_();
}

// ====================== 初始化 ======================
void TLCx543_Init(uint8_t CHIP) {
  uint8_t i;
  TLC_CHIP_TYPE = CHIP;

  TLC1542_SCLK = 0;
  TLC1542_DIN = 0;
  TLC1542_CS = 1;   // 高电平禁用
  TLC1542_DOUT = 1; // 输入模式
  TLC1542_EOC = 1;  // 输入模式

  // 首次转换，丢弃结果（TLCx543第一次读取的是不确定值）
  if (TLC_CHIP_TYPE == TLC1543) {
    TLC1542_CS = 0;
    for (i = 0; i < 10; i++) {
      TLC1542_SCLK = 1;
      SPI_Delay();
      TLC1542_SCLK = 0;
      SPI_Delay();
    }
    TLC1542_CS = 1;
    while (!TLC1542_EOC)
      ;
  } else {
    TLC1542_CS = 0;
    for (i = 0; i < 12; i++) {
      TLC1542_SCLK = 1;
      SPI_Delay();
      TLC1542_SCLK = 0;
      SPI_Delay();
    }
    TLC1542_CS = 1;
    while (!TLC1542_EOC)
      ;
  }
}

// ====================== TLC1542 10bit 读取 ======================
static uint16_t TLC1542_Read_10bit(uint8_t channel) {
  uint16_t adc = 0;
  uint8_t i;

  // 拉低 CS，等待建立时间
  TLC1542_CS = 0;
  for (i = 0; i < 4; i++)
    SPI_Delay();

  // 10个时钟：前4个发地址，同时读取上次转换结果
  for (i = 0; i < 10; i++) {
    // 前4个时钟发送地址(MSB优先)
    if (i < 4) {
      TLC1542_DIN = (channel >> (3 - i)) & 0x01;
    } else {
      TLC1542_DIN = 0;
    }

    // 上升沿锁存地址
    TLC1542_SCLK = 1;
    SPI_Delay();

    // 下降沿读取数据
    TLC1542_SCLK = 0;
    SPI_Delay();

    // 读取数据位
    adc <<= 1;
    if (TLC1542_DOUT)
      adc |= 1;
  }

  // 拉高 CS
  TLC1542_CS = 1;

  // 等待转换完成
  while (!TLC1542_EOC)
    ;

  return adc;
}

// ====================== TLC2543 12bit 读取 ======================
static uint16_t TLC2543_Read_12bit(uint8_t channel) {
  uint16_t adc = 0;
  uint8_t i;

  // 拉低 CS，等待建立时间
  TLC1542_CS = 0;
  for (i = 0; i < 4; i++)
    SPI_Delay();

  // 12个时钟：前4个发地址，同时读取上次转换结果
  for (i = 0; i < 12; i++) {
    // 前4个时钟发送地址(MSB优先)
    if (i < 4) {
      TLC1542_DIN = (channel >> (3 - i)) & 0x01;
    } else {
      TLC1542_DIN = 0;
    }

    // 上升沿锁存地址
    TLC1542_SCLK = 1;
    SPI_Delay();

    // 下降沿读取数据
    TLC1542_SCLK = 0;
    SPI_Delay();

    // 读取数据位
    adc <<= 1;
    if (TLC1542_DOUT)
      adc |= 1;
  }

  // 拉高 CS
  TLC1542_CS = 1;

  // 等待转换完成
  while (!TLC1542_EOC)
    ;

  return adc;
}

// ====================== 通用读取 ======================
uint16_t TLCx543_ReadADC(uint8_t channel) {
  if (TLC_CHIP_TYPE == TLC1543) {
    return TLC1542_Read_10bit(channel);
  } else {
    return TLC2543_Read_12bit(channel);
  }
}

// ====================== 电压计算 ======================
float TLCx543_ReadVoltageV(uint8_t channel) {
  uint16_t adc = TLCx543_ReadADC(channel);
  if (TLC_CHIP_TYPE == TLC1543) {
    return (float)adc * TLC_REF_VOLTAGE / 1023.0f;
  } else {
    return (float)adc * TLC_REF_VOLTAGE / 4095.0f;
  }
}

float TLCx543_ReadVoltageMV(uint8_t channel) {
  uint16_t adc = TLCx543_ReadADC(channel);
  if (TLC_CHIP_TYPE == TLC1543) {
    return (float)adc * TLC_REF_VOLTAGE * 1000.0f / 1023.0f;
  } else {
    return (float)adc * TLC_REF_VOLTAGE * 1000.0f / 4095.0f;
  }
}

float TLCx543_CalcHighVoltage(float m, float r) {
  if (r <= 0 || r >= 1)
    return 0;
  return m / r;
}