#include "ADC.h"

void VoltageADC_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  ADC_InitTypeDef ADC_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

  GPIO_InitStructure.GPIO_Pin =
      GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  RCC_ADCCLKConfig(RCC_PCLK2_Div6);

  ADC_DeInit(ADC1);
  ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
  ADC_InitStructure.ADC_ScanConvMode = DISABLE;
  ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
  ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
  ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
  ADC_InitStructure.ADC_NbrOfChannel = 1;
  ADC_Init(ADC1, &ADC_InitStructure);

  ADC_Cmd(ADC1, ENABLE);
  ADC_ResetCalibration(ADC1);
  while (ADC_GetResetCalibrationStatus(ADC1))
    ;
  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1))
    ;
}

uint16_t ADC_GetRaw(uint8_t ch) {
  uint8_t i;
  uint32_t sum = 0;
  uint16_t dummy;

  ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_55Cycles5);

  // --- 丢弃前3次读数（通道切换后采样电容需要充电稳定） ---
  for (i = 0; i < 3; i++) {
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
      ;
    dummy = ADC_GetConversionValue(ADC1);
    (void)dummy;
  }

  // --- 连续采样16次取平均 ---
  for (i = 0; i < 16; i++) {
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
      ;
    sum += ADC_GetConversionValue(ADC1);
  }
  return (uint16_t)(sum / 16);
}

double ADC_ConvertVoltage(uint16_t raw, double r1, double r2) {
  // V_adc = raw / 4095 * 3.3V
  // V_actual = V_adc * (R1 + R2) / R2
  double v_adc = (double)raw * ADC_VREF / ADC_MAX_VALUE;
  double v_actual = v_adc * (r1 + r2) / r2;
  return v_actual;
}

double ADC_Get800V(void) {
  uint16_t raw = ADC_GetRaw(ADC_800V_CH); // 800V通道校准
  return ADC_ConvertVoltage(raw, ADC_800V_R1, ADC_800V_R2) -
         (1.2469 - 0.206 - 0.149 - 0.601);
}

double ADC_Get600V(void) {
  uint16_t raw = ADC_GetRaw(ADC_600V_CH);
  return ADC_ConvertVoltage(raw, ADC_600V_R1, ADC_600V_R2);
}

double ADC_Get240V(void) {
  uint16_t raw = ADC_GetRaw(ADC_240V_CH);
  return ADC_ConvertVoltage(raw, ADC_240V_R1, ADC_240V_R2);
}

double ADC_Get36V(void) {
  uint16_t raw = ADC_GetRaw(ADC_36V_CH);
  return ADC_ConvertVoltage(raw, ADC_36V_R1, ADC_36V_R2);
}

double ADC_Get5V(void) {
  uint16_t raw = ADC_GetRaw(ADC_5V_CH);
  return ADC_ConvertVoltage(raw, ADC_5V_R1, ADC_5V_R2);
}