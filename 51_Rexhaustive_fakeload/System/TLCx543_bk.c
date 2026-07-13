#include "TLCx543.h"
#include <intrins.h>

uint8_t xdata TLC_CHIP_TYPE = TLC1543; // 默认10位ADC
uint16_t xdata adc;
void SPI_Delay(void) {
  _nop_();
  _nop_();
  _nop_();
  _nop_();
}

// ====================== 初始化 ======================
void TLCx543_Init(uint8_t CHIP) {
  TLC_CHIP_TYPE = CHIP;

  // 配置P4口为双向IO模式（确保P4.1/P4.4/P4.5/P4.6可输出）
  P4M0 = 0x00;
  P4M1 = 0x00;

  TLCx543_SCLK = 0;
  TLCx543_DIN = 0;
  TLCx543_CS = 1;   // 高电平禁用
  TLCx543_DOUT = 1; // 输入模式
  TLCx543_EOC = 1;  // 输入模式

  // 首次转换：发送通道0地址，启动第一次转换
  // 此时读取的数据是不确定的，将在下次读取时丢弃
  {
    unsigned char idata j;
    if (TLC_CHIP_TYPE == TLC1543) {
      TLCx543_CS = 0;
      for (j = 0; j < 4; j++)
        SPI_Delay();
      for (j = 0; j < 10; j++) {
        TLCx543_DIN = (j < 4) ? ((0 >> (3 - j)) & 0x01) : 0;
        TLCx543_SCLK = 1;
        SPI_Delay();
        TLCx543_SCLK = 0;
        SPI_Delay();
      }
      TLCx543_CS = 1;
      for (j = 0; j < 50; j++)
        SPI_Delay();
    } else {
      TLCx543_CS = 0;
      for (j = 0; j < 4; j++)
        SPI_Delay();
      for (j = 0; j < 12; j++) {
        TLCx543_DIN = (j < 4) ? ((0 >> (3 - j)) & 0x01) : 0;
        TLCx543_SCLK = 1;
        SPI_Delay();
        TLCx543_SCLK = 0;
        SPI_Delay();
      }
      TLCx543_CS = 1;
      for (j = 0; j < 50; j++)
        SPI_Delay();
    }
  }
}

static uint16_t TLC1543_Read(uint8_t channel) {
  uint8_t idata j;
  uint16_t idata result = 0;

  // 拉低 CS，等待建立时间
  TLCx543_CS = 0;
  for (j = 0; j < 4; j++)
    SPI_Delay();

  // 10个时钟：发送新地址，同时读取上次转换结果
  for (j = 0; j < TLC_CHIP_TYPE; j++) {
    // 前4个时钟发送新通道地址(MSB优先)
    if (j < 4) {
      TLCx543_DIN = (channel >> (3 - j)) & 0x01;
    } else {
      TLCx543_DIN = 0;
    }

    // 上升沿锁存地址
    TLCx543_SCLK = 1;
    SPI_Delay();

    // ★ 在上升沿读取DOUT（下降沿更新数据，上升沿读取稳定数据）
    result <<= 1;
    if (TLCx543_DOUT)
      result |= 1;

    // 下降沿：DOUT更新下一位数据
    TLCx543_SCLK = 0;
    SPI_Delay();
  }

  // 拉高 CS，启动转换
  TLCx543_CS = 1;

  // 固定延时等待转换完成（2ms @ 12MHz）
  for (j = 0; j < 50; j++) {
    SPI_Delay();
  }

  return result;
}

// ====================== 通用读取 ======================
// 注意：TLC1543是流水线架构，每次读取返回的是上一次请求通道的结果
// 因此第一次读取返回的是不确定值，需要连续读取两次才能获取正确值
uint16_t TLCx543_ReadADC(uint8_t channel) { return TLC1543_Read(channel); }

// ====================== 电压计算 ======================
float TLCx543_ReadVoltageV(uint8_t channel) {
  adc = TLCx543_ReadADC(channel);
  if (TLC_CHIP_TYPE == TLC1543) {
    return (float)adc * TLC_REF_VOLTAGE / 1023.0f;
  } else {
    return (float)adc * TLC_REF_VOLTAGE / 4095.0f;
  }
}

float TLCx543_ReadVoltageMV(uint8_t channel) {
  adc = TLCx543_ReadADC(channel);
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

// ====================== 引脚测试 ======================
// 测试P4口ADC引脚是否能正常输出高低电平（用逻辑分析仪观察）
// void TLCx543_PinTest(void) {
//   unsigned int idata delay_cnt;
//   unsigned char idata test_cnt;

//   // 配置P4口为准双向IO
//   P4M0 = 0x00;
//   P4M1 = 0x00;

//   // 初始状态
//   TLCx543_SCLK = 0;
//   TLCx543_DIN = 0;
//   TLCx543_CS = 1;
//   TLCx543_DOUT = 1;

//   for (test_cnt = 0; test_cnt < 5; test_cnt++) {
//     // === SCLK (P44) 高电平 ===
//     TLCx543_SCLK = 1;
//     for (delay_cnt = 0; delay_cnt < 30000; delay_cnt++)
//       ;
//     // === SCLK (P44) 低电平 ===
//     TLCx543_SCLK = 0;
//     for (delay_cnt = 0; delay_cnt < 30000; delay_cnt++)
//       ;

//     // === DIN (P45) 高电平 ===
//     TLCx543_DIN = 1;
//     for (delay_cnt = 0; delay_cnt < 30000; delay_cnt++)
//       ;
//     // === DIN (P45) 低电平 ===
//     TLCx543_DIN = 0;
//     for (delay_cnt = 0; delay_cnt < 30000; delay_cnt++)
//       ;

//     // === CS (P46) 低电平（低有效，故意拉低测试）===
//     TLCx543_CS = 0;
//     for (delay_cnt = 0; delay_cnt < 30000; delay_cnt++)
//       ;
//     // === CS (P46) 高电平 ===
//     TLCx543_CS = 1;
//     for (delay_cnt = 0; delay_cnt < 30000; delay_cnt++)
//       ;
//   }

//   // 恢复初始状态
//   TLCx543_SCLK = 0;
//   TLCx543_DIN = 0;
//   TLCx543_CS = 1;
//   TLCx543_DOUT = 1;
// }