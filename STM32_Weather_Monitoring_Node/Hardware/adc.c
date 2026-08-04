#include "adc.h"

void Adc_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

uint16_t Adc_Read(uint8_t channel)
{
    uint8_t adc_ch;
    if (channel == 0)
        adc_ch = ADC_Channel_5;
    else
        adc_ch = ADC_Channel_4;

    ADC_RegularChannelConfig(ADC1, adc_ch, 1, ADC_SampleTime_55Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
    return ADC_GetConversionValue(ADC1);
}

float Adc_ReadVoltage(enum Adc_Channel channel)
{
    uint16_t raw;
    float v;
    float ratio;

    raw = Adc_Read((uint8_t)channel);
    v = (float)raw * 3.3f / 4096.0f;

    if (channel == Adc_Channel_PT)
        ratio = 186.0f / 86.0f;
    else
        ratio = 78.0f / 10.0f;

    v = v * ratio;

    v = (float)((int)(v * 10000.0f + 0.5f)) / 10000.0f;
    return v;
}