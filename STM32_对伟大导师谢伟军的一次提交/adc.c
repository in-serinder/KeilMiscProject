#include "stm32f10x.h"

void ADC_init()
{
    //
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    RCC_ADCCLKConfig(RCC_PCLK2_Div6); // 72MHz / 6 = 12MHz

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;     // PA1
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN; // Analog input
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 通道1

    ADC_InitTypeDef ADC_InitStructure;
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;                  // 连续转换
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; //
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE; // 单通道

    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);

    // 校准

    ADC_ResetCalibration(ADC1); // 固定流程，内部有电路会自动执行校准
    while (ADC_GetResetCalibrationStatus(ADC1) == SET)
        ;
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) == SET)
        ;
}

float ADC_GetVoltage()
{
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
        ;
    uint16_t adcValue = ADC_GetConversionValue(ADC1);
    return (float)(adcValue * (3.3 / 4095));
}

char *ADC_GetVoltage_byStr()
{
    static char voltage_str[32] = {0};

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
        ;
    uint16_t adcValue = ADC_GetConversionValue(ADC1);
    float voltage = (float)(adcValue * (3.3 / 4095));

    uint8_t _1x = (uint8_t)((int)voltage % 10);
    uint8_t _x01 = (uint8_t)((int)voltage * 10) % 10;
    uint8_t _x001 = (uint8_t)((int)voltage * 100) % 10;

    sprintf(voltage_str, "Voltage: %d.%d%d%d V", _1x, _x01, _x001);
    return voltage_str;
}