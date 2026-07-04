#include "Rain_SENSOR.h"

// 雨量传感器模拟量引脚处于PA1(ADC1_IN1)

// ADC参考电压 (单位：mV)
#define ADC_REF_VOLTAGE 3300

/**
 * @brief  雨量传感器初始化
 * @note   配置PA1为模拟输入(ADC1_IN1)
 */
void Rain_SENSOR_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  ADC_InitTypeDef ADC_InitStructure;

  // 使能GPIOA和ADC1时钟
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

  // 配置PA1为模拟输入 (雨量传感器模拟量)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

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
 * @brief  获取雨量传感器ADC原始值
 * @retval ADC转换结果 (0-4095)
 */
uint16_t Rain_SENSOR_GetADC(void) {
  // 设置ADC通道采样时间
  ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_239Cycles5);

  // 启动ADC转换
  ADC_SoftwareStartConvCmd(ADC1, ENABLE);

  // 等待转换完成
  while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC))
    ;

  // 返回转换结果
  return ADC_GetConversionValue(ADC1);
}

/**
 * @brief  获取雨量传感器电压值
 * @retval 电压值 (单位：mV)
 */
uint16_t Rain_SENSOR_GetVoltage(void) {
  uint16_t adc_value = Rain_SENSOR_GetADC();
  // 电压 = ADC值 * 参考电压 / 4096
  return (uint16_t)((uint32_t)adc_value * ADC_REF_VOLTAGE / 4096);
}

/**
 * @brief  判断当前是否下雨
 * @retval RAIN_STATE_NO_RAIN - 无雨, RAIN_STATE_RAINING - 下雨
 */
Rain_StateTypeDef Rain_SENSOR_GetState(void) {
  uint16_t adc_value = Rain_SENSOR_GetADC();

  // 雨量传感器特性：无雨时电阻大，ADC值小；有雨时电阻小，ADC值大
  // 当ADC值大于阈值时判定为下雨
  if (adc_value >= RAIN_TH) {
    return RAIN_STATE_RAINING;
  } else {
    return RAIN_STATE_NO_RAIN;
  }
}