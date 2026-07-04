#ifndef __BATAPVADC_H__
#define __BATAPVADC_H__

#include "stm32f10x.h"

// 电池电压ADC处于PA5 分压R1=8kΩ R2=10kΩ
// 光伏电压ADC处于PB6 分压R1=68kΩ R2=10kΩ

// ADC初始化
void BatAPvADC_Init(void);

// 获取电池电压ADC原始值
uint16_t BatAPvADC_GetBatADC(void);

// 获取光伏电压ADC原始值
uint16_t BatAPvADC_GetPVADC(void);

// 获取电池电压复原值（单位：mV）
uint16_t BatAPvADC_GetBatVoltage(void);

// 获取光伏电压复原值（单位：mV）
uint16_t BatAPvADC_GetPVVoltage(void);

#endif