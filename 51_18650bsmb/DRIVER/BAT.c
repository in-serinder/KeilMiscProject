#include "BAT.h"

#define REF_VOLTAGE 5.0f // 5V参考电压
#define ADC_RESOLUTION 1024.0f // 10位ADC分辨率

#define BAT_CH1 P10 //ADC0
#define BAT_CH2 P11 //ADC1
#define BAT_CH3 P12 //ADC2
#define BAT_CH4 P13 //ADC3

/**
 * @brief ADC初始化函数
 * @note 配置P10-P13为模拟输入，使能ADC模块
 */
void BAT_ADC_Init(void)
{
    // 设置P10-P13为高阻输入模式（模拟输入）
    P1M0 &= ~(0x0F);  // P10-P13设置为高阻输入
    P1M1 |= 0x0F;
    
    // 设置ADC时钟分频，ADC时钟 = Fosc / (ADC_CLK + 1)
    // 建议ADC时钟在500kHz ~ 1.5MHz之间
    CLK_DIV &= ~(0x07);  // 清除ADC时钟分频位
    CLK_DIV |= 0x03;     // ADC_CLK = 3, ADC时钟 = Fosc/4
    
    // 使能ADC电源
    ADC_CONTR |= 0x80;  // ADC_POWER = 1
    
    // 延时等待ADC稳定
    // 实际应用中可添加适当延时
}

/**
 * @brief 读取指定通道的ADC原始值
 * @param ch: ADC通道 (BAT_CH1 ~ BAT_CH4)
 * @return 10位ADC值 (0 ~ 1023)
 */
uint16_t BAT_ADC_Read(BAT_Channel ch)
{
    uint16_t adc_value;
    
    // 选择ADC通道
    ADC_CONTR &= ~(0x0F);  // 清除通道选择位
    ADC_CONTR |= (ch & 0x0F);  // 设置通道号
    
    // 启动ADC转换
    ADC_CONTR |= 0x40;  // ADC_START = 1
    
    // 等待转换完成
    while(!(ADC_CONTR & 0x20));  // 等待ADC_FLAG = 1
    
    // 读取ADC结果
    adc_value = (uint16_t)ADC_RES << 2;
    adc_value |= (ADC_RESL & 0x03);
    
    // 清除转换完成标志
    ADC_CONTR &= ~(0x20);  // ADC_FLAG = 0
    
    return adc_value;
}

/**
 * @brief 读取指定通道的电压值
 * @param ch: ADC通道 (BAT_CH1 ~ BAT_CH4)
 * @return 电压值，单位为伏特(V)
 */
float BAT_ADC_ReadVoltage(BAT_Channel ch)
{
    uint16_t adc_value = BAT_ADC_Read(ch);
    // 电压 = (ADC值 / 分辨率) * 参考电压
    return (adc_value / ADC_RESOLUTION) * REF_VOLTAGE;
}