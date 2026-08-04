#ifndef __ADC_H__
#define __ADC_H__
#include "stm32f10x.h"


enum Adc_Channel
{
    Adc_Channel_PT = 0, //太阳能光伏板电压 PA5 分压r1 100k r2 86k
    Adc_Channel_BAT = 1, //电池电压 PA4 分压r1 68k r2 10k

};

void Adc_Init(void);
uint16_t Adc_Read(uint8_t channel); //0 读取
float Adc_ReadVoltage(enum Adc_Channel channel);
#endif 
