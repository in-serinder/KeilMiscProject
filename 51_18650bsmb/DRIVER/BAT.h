#include "stc15w.h"

#ifndef __BAT_H__
#define __BAT_H__

/**
 * @brief ADC通道枚举
 */
typedef enum {
    BAT_CH1 = 0,  // P10 - ADC0
    BAT_CH2 = 1,  // P11 - ADC1
    BAT_CH3 = 2,  // P12 - ADC2
    BAT_CH4 = 3   // P13 - ADC3
} BAT_Channel;

/**
 * @brief ADC初始化函数
 * @note 配置P10-P13为模拟输入，使能ADC模块
 */
void BAT_ADC_Init(void);

/**
 * @brief 读取指定通道的ADC原始值
 * @param ch: ADC通道 (BAT_CH1 ~ BAT_CH4)
 * @return 10位ADC值 (0 ~ 1023)
 */
uint16_t BAT_ADC_Read(BAT_Channel ch);

/**
 * @brief 读取指定通道的电压值
 * @param ch: ADC通道 (BAT_CH1 ~ BAT_CH4)
 * @return 电压值，单位为伏特(V)
 */
float BAT_ADC_ReadVoltage(BAT_Channel ch);

#endif