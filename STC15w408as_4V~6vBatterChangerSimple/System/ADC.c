#include "stc15w.h"
#include "ADC.h"
#include "Delay.h"

uint16_t getADC(uint8_t ch);
/*
6v电池检测P1.1
4v电池检测P1.0
*/
void initADC()
{

    ADC_CONTR &= ~ADCFLAG;
    P1ASF = 0xff; // 设置P1ad
    ADC_RES = 0;
    P1ASF = 0x03; // 开启P1.0~P1.1
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL | ADC_START;
    Delay1ms();
}

float getBatter_6V(void)
{
    uint16_t adcValue = getADC(1);
    float voltage = 0.0f;
    float prop = 7.2f; // 36/5v映射

    voltage = adcValue * (5 / 1023) * prop;
    return voltage;
}

float getBatter_4V(void)
{
    uint16_t adcValue = 0;
    float voltage = 0.0f;
    float prop = 4.8f;

    adcValue = getADC(0);
    voltage = adcValue * (5.0f / 1023.0f) * prop;

    return voltage;
}

uint16_t getADC(uint8_t ch)
{
    uint16_t adcvalue = 0;

    ADC_CONTR &= ~0x07;
    ADC_CONTR |= (ch & 0x07);
    ADC_CONTR |= ADC_START;

    while (!(ADC_CONTR & ADCFLAG))
        ;
    ADC_CONTR &= ~ADCFLAG;

    adcvalue = (uint16_t)ADC_RES << 2;
    adcvalue |= (uint16_t)(ADC_RESL & 0x03);

    return adcvalue;
}

float getBatter_6V_AVG(void)
{
    uint8_t count = 0;
    float voltageSum = 0.0f;
    for (count = 0; count < 10; count++)
    {
        voltageSum += getBatter_6V();
        Delay1ms();
    }
    voltageSum /= 10.0f;
    return voltageSum;
}

float getBatter_4V_AVG(void)
{
    uint8_t count = 0;
    float voltageSum = 0.0f;
    for (count = 0; count < 10; count++)
    {
        voltageSum += getBatter_4V();
        Delay1ms();
    }
    voltageSum /= 10.0f;
    return voltageSum;
}