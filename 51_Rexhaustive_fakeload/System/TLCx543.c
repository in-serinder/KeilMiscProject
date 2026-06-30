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

  TLCx543_SCLK = 0;
  TLCx543_DIN = 0;
  TLCx543_CS = 1;   // 高电平禁用
  TLCx543_DOUT = 1; // 输入模式
  TLCx543_EOC = 1;  // 输入模式

  // 首次转换：发送通道0地址，启动第一次转换
  // 此时读取的数据是不确定的，将在下次读取时丢弃
  if (TLC_CHIP_TYPE == TLC1543) {
    TLCx543_CS = 0;
    for (i = 0; i < 4; i++)
      SPI_Delay();
    for (i = 0; i < 10; i++) {
      TLCx543_DIN = (i < 4) ? ((0 >> (3 - i)) & 0x01) : 0;
      TLCx543_SCLK = 1;
      SPI_Delay();
      TLCx543_SCLK = 0;
      SPI_Delay();
    }
    TLCx543_CS = 1;
    while (!TLCx543_EOC)
      ;
  } else {
    TLCx543_CS = 0;
    for (i = 0; i < 4; i++)
      SPI_Delay();
    for (i = 0; i < 12; i++) {
      TLCx543_DIN = (i < 4) ? ((0 >> (3 - i)) & 0x01) : 0;
      TLCx543_SCLK = 1;
      SPI_Delay();
      TLCx543_SCLK = 0;
      SPI_Delay();
    }
    TLCx543_CS = 1;
    while (!TLCx543_EOC)
      ;
  }
}

static uint16_t TLC1543_Read(uint8_t channel) {
  uint16_t adc = 0;
  uint8_t i;

  // 拉低 CS，等待建立时间
  TLCx543_CS = 0;
  for (i = 0; i < 4; i++)
    SPI_Delay();

  // 10个时钟：发送新地址，同时读取上次转换结果
  for (i = 0; i < TLC_CHIP_TYPE; i++) {
    // 前4个时钟发送新通道地址(MSB优先)
    if (i < 4) {
      TLCx543_DIN = (channel >> (3 - i)) & 0x01;
    } else {
      TLCx543_DIN = 0;
    }

    // 上升沿锁存地址
    TLCx543_SCLK = 1;
    SPI_Delay();

    // 下降沿读取数据（这是上次转换的结果）
    TLCx543_SCLK = 0;
    SPI_Delay();

    // 读取数据位
    adc <<= 1;
    if (TLCx543_DOUT)
      adc |= 1;
  }

  // 拉高 CS
  TLCx543_CS = 1;

  // 等待本次转换完成（对应刚才发送的通道地址）
  while (!TLCx543_EOC)
    ;

  return adc;
}

// ====================== 通用读取 ======================
uint16_t TLCx543_ReadADC(uint8_t channel) {
  //   if (TLC_CHIP_TYPE == TLC1543) {
  //     return TLC1543_Read_10bit(channel);
  //   } else {
  //     return TLC2543_Read_12bit(channel);
  //   }
  TLC1543_Read(channel);
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