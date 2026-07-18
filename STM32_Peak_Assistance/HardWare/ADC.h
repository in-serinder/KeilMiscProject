#ifndef __ADC_H__
#define __ADC_H__

#include "stm32f10x.h"

// Vref = 3.3V, 12-bit ADC
#define ADC_VREF 3.3
#define ADC_MAX_VALUE 4095.0

// 通道1: 800V  PA4  R1=2.4M R2=10k
#define ADC_800V_CH 4
#define ADC_800V_R1 2400000.0
#define ADC_800V_R2 10000.0

// 通道2: 600V  PA3  R1=1.8M R2=10k
#define ADC_600V_CH 3
#define ADC_600V_R1 1800000.0
#define ADC_600V_R2 10000.0

// 通道3: 240V  PA2  R1=750k R2=10k
#define ADC_240V_CH 2
#define ADC_240V_R1 750000.0
#define ADC_240V_R2 10000.0

// 通道4: 36V   PA1  R1=100k R2=10k
#define ADC_36V_CH 1
#define ADC_36V_R1 100000.0
#define ADC_36V_R2 10000.0

// 通道5: 5V    PA0  R1=5.1k R2=10k
#define ADC_5V_CH 0
#define ADC_5V_R1 5100.0
#define ADC_5V_R2 10000.0
// #define ADC_5V_R1 5130.0
// #define ADC_5V_R2 98000.0

void VoltageADC_Init(void);
uint16_t ADC_GetRaw(uint8_t ch);
double ADC_ConvertVoltage(uint16_t raw, double r1, double r2);
double ADC_Get800V(void);
double ADC_Get600V(void);
double ADC_Get240V(void);
double ADC_Get36V(void);
double ADC_Get5V(void);

#endif