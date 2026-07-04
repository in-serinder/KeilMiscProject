#ifndef __RAIN_SENSOR_H__
#define __RAIN_SENSOR_H__

#include "stm32f10x.h"

// 雨量传感器模拟量引脚处于PA1(ADC1_IN1)

// 下雨阈值ADC值 (高于此值判定为下雨)
// 雨量传感器特性：无雨时电阻大，ADC值小；有雨时电阻小，ADC值大
#define RAIN_TH 2000 // 下雨阈值（可根据实际传感器调整）

// 雨量状态枚举
typedef enum {
  RAIN_STATE_NO_RAIN, // 无雨
  RAIN_STATE_RAINING  // 下雨
} Rain_StateTypeDef;

/**
 * @brief  雨量传感器初始化
 */
void Rain_SENSOR_Init(void);

/**
 * @brief  获取雨量传感器ADC原始值
 * @retval ADC转换结果 (0-4095)
 */
uint16_t Rain_SENSOR_GetADC(void);

/**
 * @brief  获取雨量传感器电压值
 * @retval 电压值 (单位：mV)
 */
uint16_t Rain_SENSOR_GetVoltage(void);

/**
 * @brief  判断当前是否下雨
 * @retval RAIN_STATE_NO_RAIN - 无雨, RAIN_STATE_RAINING - 下雨
 */
Rain_StateTypeDef Rain_SENSOR_GetState(void);

#endif