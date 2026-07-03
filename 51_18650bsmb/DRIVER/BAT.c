#include "BAT.h"

#define REF_VOLTAGE 5.0f       // 5V参考电压
#define ADC_RESOLUTION 1024.0f // 10位ADC分辨率

#define BAT_CH1 P10 // ADC0
#define BAT_CH2 P11 // ADC1
#define BAT_CH3 P12 // ADC2
#define BAT_CH4 P13 // ADC3

/**
 * @brief ADC初始化函数
 * @note 配置P10-P13为模拟输入，使能ADC模块
 */
void BAT_ADC_Init(void) {
  // 设置P10-P13为高阻输入模式（模拟输入）
  P1M0 &= ~(0x0F); // P10-P13设置为高阻输入
  P1M1 |= 0x0F;

  // 设置ADC时钟分频，ADC时钟 = Fosc / (ADC_CLK + 1)
  // 建议ADC时钟在500kHz ~ 1.5MHz之间
  CLK_DIV &= ~(0x07); // 清除ADC时钟分频位
  CLK_DIV |= 0x03;    // ADC_CLK = 3, ADC时钟 = Fosc/4

  // 使能ADC电源
  ADC_CONTR |= 0x80; // ADC_POWER = 1

  // 延时等待ADC稳定
  // 实际应用中可添加适当延时
}

/**
 * @brief 读取指定通道的ADC原始值
 * @param ch: ADC通道 (BAT_CH1 ~ BAT_CH4)
 * @return 10位ADC值 (0 ~ 1023)
 */
uint16_t BAT_ADC_Read(BAT_Channel ch) {
  uint16_t adc_value;

  // 选择ADC通道
  ADC_CONTR &= ~(0x0F);     // 清除通道选择位
  ADC_CONTR |= (ch & 0x0F); // 设置通道号

  // 启动ADC转换
  ADC_CONTR |= 0x40; // ADC_START = 1

  // 等待转换完成
  while (!(ADC_CONTR & 0x20))
    ; // 等待ADC_FLAG = 1

  // 读取ADC结果
  adc_value = (uint16_t)ADC_RES << 2;
  adc_value |= (ADC_RESL & 0x03);

  // 清除转换完成标志
  ADC_CONTR &= ~(0x20); // ADC_FLAG = 0

  return adc_value;
}

/**
 * @brief 读取指定通道的电压值
 * @param ch: ADC通道 (BAT_CH1 ~ BAT_CH4)
 * @return 电压值，单位为伏特(V)
 */
float BAT_ADC_ReadVoltage(BAT_Channel ch) {
  uint16_t adc_value = BAT_ADC_Read(ch);
  // 电压 = (ADC值 / 分辨率) * 参考电压
  return (adc_value / ADC_RESOLUTION) * REF_VOLTAGE;
}

// 电量估算
float VoltageToSOC(float ocv) {
  uint8_t i, s1, s2;
  float v1, v2, ratio, soc;
  // SOC数组
  float v_table[] = {4.20f, 4.06f, 3.98f, 3.92f, 3.87f, 3.82f,
                     3.79f, 3.77f, 3.74f, 3.68f, 3.45f, 3.00f};
  uint8_t soc_table[] = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 5, 0};
  uint8_t len = sizeof(v_table) / sizeof(float);

  // 满电/亏电边界
  if (ocv >= 4.20f)
    return 100;
  if (ocv <= 3.00f)
    return 0;

  // 查找区间
  for (i = 0; i < len - 1; i++) {
    v1 = v_table[i];
    v2 = v_table[i + 1];
    s1 = soc_table[i];
    s2 = soc_table[i + 1];

    if (ocv <= v1 && ocv >= v2) {
      // 线性插值
      ratio = (v1 - ocv) / (v1 - v2);
      soc = s1 - ratio * (s1 - s2);
      return (uint8_t)(soc + 0.5f);
    }
  }
  return 0;
}