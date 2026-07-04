#ifndef __LIGHT_SENSOR_H__
#define __LIGHT_SENSOR_H__

#include "stm32f10x.h"

// 光敏传感器模拟量引脚处于PA0(ADC1_IN0) 数字引脚为PB0

// 夜间阈值ADC值 (低于此值判定为夜间)
// 光敏电阻特性：光照越强，电阻越小，ADC值越小
// 光照越弱，电阻越大，ADC值越大
#define NIGHT_TH 3000 // 夜间阈值（可根据实际传感器调整）

// 传感器状态枚举
typedef enum {
  LIGHT_STATE_DAY,  // 白天
  LIGHT_STATE_NIGHT // 夜间
} Light_StateTypeDef;

/**
 * @brief  光敏传感器初始化
 */
void Light_SENSOR_Init(void);

/**
 * @brief  获取光敏传感器ADC原始值
 * @retval ADC转换结果 (0-4095)
 */
uint16_t Light_SENSOR_GetADC(void);

/**
 * @brief  获取光敏传感器电压值
 * @retval 电压值 (单位：mV)
 */
uint16_t Light_SENSOR_GetVoltage(void);

/**
 * @brief  判断当前是白天还是夜间
 * @retval LIGHT_STATE_DAY - 白天, LIGHT_STATE_NIGHT - 夜间
 */
Light_StateTypeDef Light_SENSOR_GetState(void);

#endif