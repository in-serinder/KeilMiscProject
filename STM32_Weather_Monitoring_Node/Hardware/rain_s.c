#include "rain_s.h"

#define RAIN_SENSOR_PIN   GPIO_Pin_1
#define RAIN_SENSOR_PORT  GPIOA
#define RAIN_SENSOR_ADC   ADC1
#define RAIN_SENSOR_CH    ADC_Channel_1   /* PA1 = ADC1_IN1 */

/* 注意: 旧版本代码误用了 ADC_Channel_0 (对应 PA0), 本文件修正为 Channel_1 (PA1) */

static uint8_t lastHits = 0;  /* 调试用: 上一轮命中了几次 */

/* ---- 简单毫秒级阻塞延时 (SysTick 8MHz/72MHz 都能用, 粗延时足够放电) ---- */
static void Rain_DelayMs(uint32_t ms)
{
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 8000; j++) { /* 约 1ms @72MHz, 保守即可 */
            __NOP();
        }
}

/* ---- 把 PA1 临时切换为 推挽输出 0V (预放电), 释放引脚残留电荷 ----
 * 这样即便外部完全浮空: 采样电容/寄生电容先被拉到 0V.
 * 真有水膜 (模式1 3V3 接过来): 放电后水膜很快把引脚重新充到高压 -> 读出来就是高
 * 真浮空(无雨): 没有任何驱动 -> 引脚电压会在低/中值慢慢漂, 不会稳定在高值
 */
static void Rain_Pin_PreDrain(void)
{
    GPIO_InitTypeDef g;
    /* 推挽输出, 低 */
    g.GPIO_Pin   = RAIN_SENSOR_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RAIN_SENSOR_PORT, &g);
    GPIO_ResetBits(RAIN_SENSOR_PORT, RAIN_SENSOR_PIN);
    Rain_DelayMs(RAIN_SENSOR_PRE_DRAIN_MS);
}

/* ---- 把 PA1 切回模拟输入 AIN ---- */
static void Rain_Pin_SetAnalog(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin  = RAIN_SENSOR_PIN;
    g.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(RAIN_SENSOR_PORT, &g);
}

void Rain_Sensor_Init(void)
{
    GPIO_InitTypeDef g;
    ADC_InitTypeDef  a;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    /* 上电先切 AIN 模拟输入 */
    g.GPIO_Pin  = RAIN_SENSOR_PIN;
    g.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(RAIN_SENSOR_PORT, &g);

    a.ADC_Mode              = ADC_Mode_Independent;
    a.ADC_ScanConvMode      = DISABLE;
    a.ADC_ContinuousConvMode = DISABLE;
    a.ADC_ExternalTrigConv  = ADC_ExternalTrigConv_None;
    a.ADC_DataAlign         = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel      = 1;
    ADC_Init(RAIN_SENSOR_ADC, &a);

    ADC_Cmd(RAIN_SENSOR_ADC, ENABLE);
    ADC_ResetCalibration(RAIN_SENSOR_ADC);
    while (ADC_GetResetCalibrationStatus(RAIN_SENSOR_ADC));
    ADC_StartCalibration(RAIN_SENSOR_ADC);
    while (ADC_GetCalibrationStatus(RAIN_SENSOR_ADC));
}

/* 单次采样: 先放电 -> 切AIN -> 立即ADC -> 返回mV */
static uint16_t Rain_SingleSample_mV(void)
{
    uint16_t raw;
    /* 1) 预放电 */
    Rain_Pin_PreDrain();
    /* 2) 切回 AIN */
    Rain_Pin_SetAnalog();
    /* 3) 立即启动转换 (避免给浮空引脚太多时间耦合到高电平) */
    ADC_RegularChannelConfig(RAIN_SENSOR_ADC,
                             RAIN_SENSOR_CH,
                             1,
                             ADC_SampleTime_71Cycles5); /* 略长采样时间更稳 */
    ADC_SoftwareStartConvCmd(RAIN_SENSOR_ADC, ENABLE);
    while (ADC_GetFlagStatus(RAIN_SENSOR_ADC, ADC_FLAG_EOC) == RESET);
    raw = ADC_GetConversionValue(RAIN_SENSOR_ADC);
    return (uint16_t)((uint32_t)raw * 3300 / 4096);
}

/* 命中阈值一次算 1 hit */
static inline bool Rain_IsHit_mV(uint16_t mv)
{
#if RAIN_SENSOR_POLARITY == 1
    /* 模式1: 3V3 -> 板 -> PA1.  有水=被拉高, mv >= 阈值 算命中 */
    return (mv >= RAIN_SENSOR_RAIN_THRESHOLD);
#else
    /* 模式2: GND -> 板 -> PA1.  有水=被拉低, mv <  阈值 算命中  */
    return (mv <  RAIN_SENSOR_RAIN_THRESHOLD);
#endif
}
uint16_t Rain_Sensor_Read(void)
{
    uint8_t i;
    uint16_t samples[RAIN_SENSOR_SAMPLES];
    uint8_t hits = 0;
    uint16_t extreme; /* 模式1存最高, 模式2存最低 */

    for (i = 0; i < RAIN_SENSOR_SAMPLES; i++) {
        samples[i] = Rain_SingleSample_mV();
        if (Rain_IsHit_mV(samples[i])) hits++;
    }
    lastHits = hits;

    /* 求极值返回, 方便主逻辑看"最极端"的一次读数 */
    extreme = samples[0];
    for (i = 1; i < RAIN_SENSOR_SAMPLES; i++) {
#if RAIN_SENSOR_POLARITY == 1
        if (samples[i] > extreme) extreme = samples[i];
#else
        if (samples[i] < extreme) extreme = samples[i];
#endif
    }
    return extreme;
}

bool Rain_Sensor_IsRain(void)
{
    /* 触发一次完整多采样, lastHits 会被更新 */
    (void)Rain_Sensor_Read();
    /* 命中次数达标才认定真下雨, 排除浮空的偶发高值/低值噪声 */
    return (lastHits >= RAIN_SENSOR_HIT_COUNT);
}

uint8_t Rain_Sensor_LastHits(void)
{
    return lastHits;
}