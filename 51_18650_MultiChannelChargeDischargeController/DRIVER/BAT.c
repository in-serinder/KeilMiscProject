#include "BAT.h"
#include "delay.h"
#include <intrins.h>

#define REF_VOLTAGE 5.0f
#define ADC_RESOLUTION 1024.0f

sfr ADCCFG = 0xDE;
sfr ADCTIM = 0xDF;

/**
 * @brief 50KSPS@11.0592MHz
 */
void AdcSetRate(void) {
  ADCCFG &= ~0x0f;
  ADCCFG |= 0x02;
  ADCTIM = 0x35;
}

/**
 * @brief ADC初始化
 */
void BAT_ADC_Init(void) {
  uint8_t i;
  uint16_t dummy;

  CH1_PIN = 0;
  CH2_PIN = 0;
  CH3_PIN = 0;
  CH4_PIN = 0;

  P1M0 &= ~0x0F;
  P1M1 |= 0x0F;
  P1ASF |= 0x0F;
  AUXR |= 0x01;

  AdcSetRate();

  ADC_CONTR = 0x80;

  for (i = 0; i < 200; i++)
    delay_ms(1); // 约200ms

  for (i = 0; i < 10; i++) {
    dummy = BAT_ADC_Read((BAT_Channel)0);
    (void)dummy;
  }
}

uint16_t BAT_ADC_Read(BAT_Channel ch) {
  // uint8_t ch_num = (uint8_t)ch & 0x07;
  // uint16_t timeout;

  // // POWER=1, START=1, SPEED=540clk, CHS=ch
  // ADC_CONTR = 0x88 | ch_num;
  // _nop_();
  // _nop_();
  // _nop_();
  // _nop_();

  // timeout = 0;
  // while (!(ADC_CONTR & 0x10)) {
  //   if (++timeout > 30000) {
  //     ADC_CONTR = 0x80 | ch_num; // 超时停止
  //     return 0;
  //   }
  // }

  // ADC_CONTR = 0x80 | ch_num;

  // // 右对齐：ADC_RES*4 + ADC_RESL
  // return ((uint16_t)ADC_RES << 2) | (ADC_RESL & 0x03);

  uint8_t ch_num = (uint8_t)ch & 0x07;
  uint16_t timeout;

  ADC_CONTR = 0x88 | ch_num; // POWER=1, START=1
  _nop_();
  _nop_();
  _nop_();
  _nop_();

  timeout = 0;
  while (!(ADC_CONTR & 0x10)) {
    if (++timeout > 30000) {
      ADC_CONTR &= ~0x40; // 超时则停止
      return 0;
    }
  }
  ADC_CONTR &= ~0x10; // 清除标志
  ADCCFG |= 0x20;     // 设置 ADC 结果右对齐
  // START 在转换完成后自动清零
  return ((uint16_t)ADC_RES << 2) | (ADC_RESL & 0x03);
}

/**
 * @brief 读取电压值
 *        采样10次→排序→去最大最小→剩余8个再去偏离中位数超30LSB的异常值→取平均
 * @param ch: ADC通道
 * @return 电池实际电压(V)
 */
float BAT_ADC_ReadVoltage(BAT_Channel ch) {
  uint8_t i, j, t;
  uint16_t buf[10];
  uint16_t tmp;
  uint32_t sum;
  uint16_t mid;
  float v;

  (void)BAT_ADC_Read(ch);

  for (i = 0; i < 10; i++)
    buf[i] = BAT_ADC_Read(ch);

  for (i = 0; i < 9; i++) {
    for (j = 0; j < 9 - i; j++) {
      if (buf[j] > buf[j + 1]) {
        tmp = buf[j];
        buf[j] = buf[j + 1];
        buf[j + 1] = tmp;
      }
    }
  }

  mid = (buf[4] + buf[5]) / 2;

  sum = 0;
  t = 0;
  for (i = 1; i <= 8; i++) {
    if (buf[i] >= mid - 30 && buf[i] <= mid + 30) {
      sum += buf[i];
      t++;
    }
  }

  if (t == 0) {
    // 极端情况：所有值都被过滤，直接用中位数
    v = ((float)mid / ADC_RESOLUTION) * REF_VOLTAGE;
  } else {
    v = ((float)sum / (float)t / ADC_RESOLUTION) * REF_VOLTAGE;
  }
  return v;
}

/**
 * @brief 电压转SOC（整数运算，用mV避免float精度问题）
 * @param ocv: 电压(V)
 * @return SOC (0~100)
 *
 * LiFePO4/锂电压典型曲线(简化)：
 *   4200mV → 100%
 *   4100mV → 90%
 *   4000mV → 75%
 *   3900mV → 55%
 *   3800mV → 40%
 *   3700mV → 30%
 *   3600mV → 20%
 *   3500mV → 12%
 *   3400mV → 7%
 *   3300mV → 3%
 *   3000mV → 0%
 */
uint8_t VoltageToSOC(float ocv) {
  uint16_t mv; // 电压(mV)
  uint8_t i;
  uint16_t v1, v2; // 区间两端电压(mV)
  uint8_t s1, s2;  // 区间两端SOC(%)
  uint16_t vd, sd; // 区间差值
  uint32_t tmp;

  // 将 V 转为 mV（四舍五入）
  mv = (uint16_t)(ocv * 1000.0f + 0.5f);

  // 边界保护
  if (mv >= 4200)
    return 100;
  if (mv <= 3000)
    return 0;

  // 简化查表（12个点）
  {
    uint16_t code v_tb[12] = {4200, 4100, 4000, 3900, 3800, 3700,
                              3600, 3500, 3400, 3300, 3200, 3000};
    uint8_t code s_tb[12] = {100, 90, 75, 55, 40, 30, 20, 12, 7, 3, 1, 0};

    for (i = 0; i < 11; i++) {
      v1 = v_tb[i];
      v2 = v_tb[i + 1];
      s1 = s_tb[i];
      s2 = s_tb[i + 1];

      // mv 在 [v2, v1] 区间内？
      if (mv <= v1 && mv >= v2) {
        // 线性插值：SOC = s1 - (s1 - s2) * (v1 - mv) / (v1 - v2)
        vd = v1 - v2;
        sd = s1 - s2;
        if (vd == 0)
          return s1;
        tmp = (uint32_t)(v1 - mv) * sd;
        tmp = tmp / vd;
        return (uint8_t)(s1 - tmp);
      }
    }
  }
  return 0;
}