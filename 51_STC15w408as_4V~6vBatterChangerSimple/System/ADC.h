#ifndef __ADC_H_
#define __ADC_H_

#define ADC_POWER 0x80
// ADC电源控制位
#define ADCFLAG 0x10
// IADC完成标志
#define ADC_START 0x08
// IADC起始控制位
#define ADC_SPEEDLL 0x00
// 540个时钟
#define ADC_SPEEDL 0x20
// 360个时钟
#define ADC_SPEEDH 0x40
// 180个时钟
#define ADC_SPEEDHH Ox60
// 90个时钟

typedef unsigned char uint8_t;
typedef unsigned int uint16_t;

void initADC();
float getBatter_6V(void);
float getBatter_4V(void);
float getBatter_6V_AVG(void);
float getBatter_4V_AVG(void);
#endif