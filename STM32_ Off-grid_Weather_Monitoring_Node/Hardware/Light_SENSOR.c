#include "Light_SENSOR.h"

// 光敏传感器模拟量引脚处于PA0(ADC1_IN0) 数字引脚为PB0

// ADC参考电压 (单位：mV)
#define ADC_REF_VOLTAGE 3300

/**
 * @brief  光敏传感器初始化
 * @note   配置PA0为模拟输入(ADC)，PB0为数字输入(可选)
 */
void Light_SENSOR_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  ADC_InitTypeDef ADC_InitStructure;

  // 使能GPIOA、GPIOB和ADC1时钟
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                             RCC_APB2Periph_ADC1,
                         ENABLE);

  // 配置PA0为模拟输入 (光敏传感器模拟量)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // 配置PB0为上拉输入 (光敏传感器数字量)
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
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
 * @brief  获取光敏传感器ADC原始值
 * @retval ADC转换结果 (0-4095)
 */
uint16_t Light_SENSOR_GetADC(void) {
  // 设置ADC通道采样时间
  ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);

  // 启动ADC转换
  ADC_SoftwareStartConvCmd(ADC1, ENABLE);

  // 等待转换完成
  while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC))
    ;

  // 返回转换结果
  return ADC_GetConversionValue(ADC1);
}

/**
 * @brief  获取光敏传感器电压值
 * @retval 电压值 (单位：mV)
 */
uint16_t Light_SENSOR_GetVoltage(void) {
  uint16_t adc_value = Light_SENSOR_GetADC();
  // 电压 = ADC值 * 参考电压 / 4096
  return (uint16_t)((uint32_t)adc_value * ADC_REF_VOLTAGE / 4096);
}

/**
 * @brief  判断当前是白天还是夜间
 * @retval LIGHT_STATE_DAY - 白天, LIGHT_STATE_NIGHT - 夜间
 */
Light_StateTypeDef Light_SENSOR_GetState(void) {
  uint16_t adc_value = Light_SENSOR_GetADC();

  // 光敏电阻特性：光照越强，电阻越小，ADC值越小
  // 光照越弱，电阻越大，ADC值越大
  // 当ADC值大于阈值时判定为夜间
  if (adc_value >= NIGHT_TH) {
    return LIGHT_STATE_NIGHT;
  } else {
    return LIGHT_STATE_DAY;
  }
}