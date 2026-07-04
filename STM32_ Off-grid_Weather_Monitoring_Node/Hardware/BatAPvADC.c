#include "BatAPvADC.h"

// 电池电压ADC处于PA5 分压R1=8kΩ R2=10kΩ
// 光伏电压ADC处于PB6 分压R1=68kΩ R2=10kΩ

// 电压计算系数 (使用整数运算避免浮点)
// 电池电压: Vbat = ADC * 3.3V * (8+10)/10 / 4096 = ADC * 3300 * 1.8 / 4096 =
// ADC * 5940 / 4096
#define BAT_DIV_RATIO_NUM 5940 // 分压比分子 (3300 * 1.8)
#define BAT_DIV_RATIO_DEN 4096 // 分母

// 光伏电压: Vpv = ADC * 3.3V * (68+10)/10 / 4096 = ADC * 3300 * 7.8 / 4096 =
// ADC * 25740 / 4096
#define PV_DIV_RATIO_NUM 25740 // 分压比分子 (3300 * 7.8)
#define PV_DIV_RATIO_DEN 4096  // 分母

/**
 * @brief  ADC初始化
 * @note   配置PA5(ADC1_IN5)和PB6(ADC1_IN13)为模拟输入
 */
void BatAPvADC_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  ADC_InitTypeDef ADC_InitStructure;

  // 使能GPIOA、GPIOB和ADC1时钟
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                             RCC_APB2Periph_ADC1,
                         ENABLE);

  // 配置PA5为模拟输入 (电池电压ADC)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // 配置PB6为模拟输入 (光伏电压ADC)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
  GPIO_Init(GPIOB, &GPIO_InitStructure);

  // ADC配置
  ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
  ADC_InitStructure.ADC_ScanConvMode = DISABLE;
  ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
  ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
  ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
  ADC_InitStructure.ADC_NbrOfChannel = 1;
  ADC_Init(ADC1, &ADC_InitStructure);

  // 使能ADC1
  ADC_Cmd(ADC1, ENABLE);

  // 校准ADC
  ADC_ResetCalibration(ADC1);
  while (ADC_GetResetCalibrationStatus(ADC1))
    ;
  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1))
    ;
}

/**
 * @brief  读取指定ADC通道的值
 * @param  ch: ADC通道 (如ADC_Channel_5, ADC_Channel_13)
 * @retval ADC转换结果 (0-4095)
 */
static uint16_t BatAPvADC_ReadChannel(uint8_t ch) {
  // 设置ADC通道采样时间
  ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5);

  // 启动ADC转换
  ADC_SoftwareStartConvCmd(ADC1, ENABLE);

  // 等待转换完成
  while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC))
    ;

  // 返回转换结果
  return ADC_GetConversionValue(ADC1);
}

/**
 * @brief  获取电池电压ADC原始值
 * @retval ADC转换结果 (0-4095)
 */
uint16_t BatAPvADC_GetBatADC(void) {
  return BatAPvADC_ReadChannel(ADC_Channel_5);
}

/**
 * @brief  获取光伏电压ADC原始值
 * @retval ADC转换结果 (0-4095)
 */
uint16_t BatAPvADC_GetPVADC(void) {
  return BatAPvADC_ReadChannel(ADC_Channel_13);
}

/**
 * @brief  获取电池电压复原值
 * @retval 电池电压 (单位：mV)
 */
uint16_t BatAPvADC_GetBatVoltage(void) {
  uint16_t adc_value = BatAPvADC_GetBatADC();
  // Vbat = ADC * 3300mV * (R1+R2)/R2 / 4096 = ADC * 5940 / 4096
  return (uint16_t)((uint32_t)adc_value * BAT_DIV_RATIO_NUM /
                    BAT_DIV_RATIO_DEN);
}

/**
 * @brief  获取光伏电压复原值
 * @retval 光伏电压 (单位：mV)
 */
uint16_t BatAPvADC_GetPVVoltage(void) {
  uint16_t adc_value = BatAPvADC_GetPVADC();
  // Vpv = ADC * 3300mV * (R1+R2)/R2 / 4096 = ADC * 25740 / 4096
  return (uint16_t)((uint32_t)adc_value * PV_DIV_RATIO_NUM / PV_DIV_RATIO_DEN);
}