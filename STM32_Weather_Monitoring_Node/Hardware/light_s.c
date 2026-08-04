#include "light_s.h"

#define LIGHT_SENSOR_NIGHT_THRESHOLD 1000
#define LIGHT_SENSOR_ANG_PIN GPIO_Pin_0
#define LIGHT_SENSOR_DIG_PIN GPIO_Pin_0
#define LIGHT_SENSOR_ANG_PORT GPIOA
#define LIGHT_SENSOR_DIG_PORT GPIOB


void Light_Sensor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_InitStructure.GPIO_Pin = LIGHT_SENSOR_ANG_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(LIGHT_SENSOR_ANG_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = LIGHT_SENSOR_DIG_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LIGHT_SENSOR_DIG_PORT, &GPIO_InitStructure);

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

uint16_t Light_Sensor_Read(void)
{
    uint16_t raw;
    uint32_t mv;
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_55Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
    raw = ADC_GetConversionValue(ADC1);
    mv = (uint32_t)raw * 3300 / 4096;
    return (uint16_t)mv;
}

bool Light_Sensor_IsNight(void)
{
    if (GPIO_ReadInputDataBit(LIGHT_SENSOR_DIG_PORT, LIGHT_SENSOR_DIG_PIN) == 0)
        return true;
    return Light_Sensor_Read() <= LIGHT_SENSOR_NIGHT_THRESHOLD;
}

float Light_Sensor_ReadVoltage(void)
{
    uint16_t mv = Light_Sensor_Read();
    float v = (float)mv / 1000.0f;
    v = (float)((int)(v * 10000.0f + 0.5f)) / 10000.0f;
    return v;
}