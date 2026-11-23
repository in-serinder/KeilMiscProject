/* =====================================================================
 * adc.c  —  STC8G 内置 10 位 ADC（MQ 传感器）
 *
 * 寄存器要点（手册 16.1，STC8G1K08A 系列）：
 *   ADC_CONTR  B7 ADC_POWER  B6 ADC_START  B5 ADC_FLAG  B4 ADC_EPWMT  B3~B0 ADC_CHS
 *   ADCCFG     B5 RESFMT（0=左对齐 1=右对齐） B3~B0 SPEED，FADC = SYSclk/2/(SPEED+1)
 *   ADCTIM     B7 CSSETUP  B6~B5 CSHOLD  B4~B0 SMPDUTY
 *              官方推荐 0x3F（SMPDUTY=31，手册要求"不能小于 10"）
 *
 * ★ 分辨率为 10 位 -> 结果 0~1023（不是 4095）。
 *   右对齐时 ADC_RES 的低 2 位是高 2 位、ADC_RESL 是低 8 位，
 *   所以拼法 ((ADC_RES & 0x0F) << 8) | ADC_RESL 对 10/12 位都正确。
 *
 * ★ ADCTIM 在 xdata 扩展 SFR 区（0xFEA8），访问前必须 P_SW2 |= 0x80（EAXFR），
 *   否则 0x3F 会落到普通 xdata 上，ADC 一直用复位默认时序。
 *
 * ★ ADC_POWER 打开后要等约 1ms 电源稳定才能启动转换（手册明确要求）。
 * ===================================================================== */
#include "adc.h"
#include "delay.h"

static uint8_t  s_ch = MQ_ADC_CH;       /* 实际使用的通道 */
static uint8_t  s_present;              /* 1 = 片上确实有 ADC 模块 */
static uint8_t  s_cfg = 0x2F;           /* 实际生效的 ADCCFG（自检时可能会换一套） */
static uint8_t  s_digital;              /* 1 = 走数字 DO（ADC 不可用时的降级） */
static uint8_t  s_last_to;              /* 上一次 MQ_Read 的超时次数 */
static uint16_t s_last_guard;           /* 上一次成功转换的空转次数 */

#if CFG_DEBUG_UART
/* 开机自检时抓下来的寄存器回读值，供 main 打串口 */
static uint8_t s_pr[9];                 /* 0=ADC_CONTR(写 0A) 1=ADC_CONTR(写 82 后 1ms) */
                                        /* 2=ADCCFG 3=ADCTIM 4=P3M1 5=IE2 6=WDT_CONTR
                                           7=CLKDIV 8=ADC_CONTR(EAXFR=0 时写 0A) */
#endif

void ADC_Init(void)
{
#if CFG_MQ_DIGITAL
    /* 强制数字 DO 模式：P3.2 当普通输入（准双向 + 写 1，外部比较器输出可拉低） */
    P3M1 &= (uint8_t)~0x04;
    P3M0 &= (uint8_t)~0x04;
    MQ_ADC_PIN = 1;
    s_present = 0;
    s_digital = 1;
    return;
#else
    /* 1) 寄存器存在性自检
     *    先写一个"不像配置值"的图案 0x5A，再读回来：
     *    真 A 片子（有 ADC）会保留实现的位，读回非 0；
     *    没有 ADC 模块的片子（STC8G1K08-8Pin 无 A）该地址不存在，读回 0x00。
     *    P3M1 作为对照组（一定存在），证明"这一路 SFR 写读是通的"。 */
    P_SW2 |= 0x80;                  /* 打开扩展 SFR 访问（ADCTIM 用） */
    P3M1 |= 0x04;                   /* 顺便把 P3.2 设成高阻输入 */
#if CFG_DEBUG_UART
    s_pr[4] = P3M1;
#endif

    /* 图案用 0x0A（通道 10，不带 POWER/START/EPWMT），
     * 真寄存器会读回 0x0A，不存在的地址读回 0x00。 */
    ADC_CONTR = 0x0A;
#if CFG_DEBUG_UART
    s_pr[0] = ADC_CONTR;

    /* 周边 SFR 对照：IE2(0xAF) / WDT_CONTR(0xC1) 是普通 SFR，
     * CLKDIV(0xFE01) 在 xdata SFR 区（验证 EAXFR 是否真的开了）。
     * 这几个能读回写进去的值、而 ADC 那组读回 0，就说明只是 ADC 块缺失。 */
    IE2 = 0x00;
    s_pr[5] = IE2;
    WDT_CONTR = 0x00;
    s_pr[6] = WDT_CONTR;
    CLKDIV = 0x00;                      /* 分频 1，等于复位值，安全 */
    s_pr[7] = CLKDIV;

    /* 最后排除一种可能：EAXFR(bit7)=1 会不会让普通 SFR 读不回来。
     * 关掉 EAXFR 再写一次 ADC_CONTR，如果这时能读回 0x5A，那就是 EAXFR 的锅。 */
    P_SW2 &= 0x7F;
    ADC_CONTR = 0x0A;
    s_pr[8] = ADC_CONTR;
    P_SW2 |= 0x80;
#endif

    /* 2) 正经配置：官方推荐值 */
    ADCTIM = 0x3F;                  /* CSSETUP=0, CSHOLD=01, SMPDUTY=31 */
    ADCCFG = 0x20 | 0x0F;           /* 右对齐 + FADC = SYSclk/2/16 */
#if CFG_DEBUG_UART
    s_pr[2] = ADCCFG;
    s_pr[3] = ADCTIM;
#endif

    ADC_CONTR = 0x80 | (s_ch & 0x0F);   /* 打开 ADC 电源 + 选通道 */
    delay_ms(2);                        /* 等 ADC 电源稳定（手册要求 >=1ms） */
#if CFG_DEBUG_UART
    s_pr[1] = ADC_CONTR;
#endif

    /* 3) 用内部 1.19V 基准（通道 15）判定 ADC 到底有没有在转换：
     *    这一路与外部引脚无关，能转换就说明 ADC 模块是活的。 */
    if ((ADC_RawRead(15) != 0xFFFF) || (ADC_RawRead(s_ch) != 0xFFFF))
    {
        s_cfg = 0x2F;
        s_present = 1;
        return;
    }

    /* 4) 退一步：换成官方例程那套（左对齐 + 复位默认 ADCTIM）再试一次，
     *    排除某些批次对时序寄存器敏感的可能。 */
    P_SW2 |= 0x80;
    ADCTIM = 0x2A;
    ADCCFG = 0x0F;
    ADC_CONTR = 0x80 | (s_ch & 0x0F);
    delay_ms(2);

    if ((ADC_RawRead(15) != 0xFFFF) || (ADC_RawRead(s_ch) != 0xFFFF))
    {
        s_cfg = 0x0F;
        s_present = 1;
        return;
    }

    s_cfg = 0x2F;
    s_present = 0;

#if CFG_MQ_AUTO_DO
    /* 两次配置都转不动：ADC 这块在这颗片子上不可用，自动降级到数字 DO，
     * 至少让报警功能能跑（阈值由 MQ 模块的电位器决定）。 */
    s_digital = 1;
    P3M1 &= (uint8_t)~0x04;
    P3M0 &= (uint8_t)~0x04;
    MQ_ADC_PIN = 1;
#endif
#endif
}

#if CFG_MQ_AUTO_DO || CFG_MQ_DIGITAL
uint8_t ADC_IsDigital(void)
{
    return s_digital;
}
#endif

#if !CFG_MQ_DIGITAL
uint8_t ADC_IsPresent(void)
{
    return s_present;
}
#endif

#if CFG_DEBUG_UART && !CFG_MQ_DIGITAL
uint8_t ADC_GetCfg(void)
{
    return s_cfg;
}

uint8_t ADC_ProbeReg(uint8_t idx)
{
    if (idx > 8)
    {
        return 0;
    }
    return s_pr[idx];
}
#endif

void ADC_SetCh(uint8_t ch)
{
    s_ch = (uint8_t)(ch & 0x0F);
#if !CFG_MQ_DIGITAL
    ADC_CONTR = 0x80 | s_ch;            /* 立即切到新通道并保持上电 */
#endif
}

#if CFG_DEBUG_UART
uint8_t ADC_GetCh(void)
{
    return s_ch;
}

uint8_t ADC_LastTo(void)
{
    return s_last_to;
}

uint16_t ADC_LastGuard(void)
{
    return s_last_guard;
}
#endif

uint16_t ADC_RawRead(uint8_t ch)
{
    uint16_t v;
    uint16_t guard = 0;

    ADC_CONTR = 0x80 | (ch & 0x0F) | 0x40;  /* 保持上电，启动转换 */
    _nop_();
    _nop_();
    _nop_();
    _nop_();

    while ((ADC_CONTR & 0x20) == 0)
    {
        /* 正常 30~60us 就置位。给到约 4ms 还没置位 = ADC 根本没在转换
         * （片子没有 ADC / 通道未引出 / 外设没使能），返回 0xFFFF 让上层显示 Err，
         * 不要静默当成 0 —— 否则"输入 0V"和"ADC 没跑"看起来一模一样。 */
        if (++guard > CFG_ADC_GUARD)
        {
            ADC_CONTR &= (uint8_t)~0x40;    /* 撤掉 START，别把外设卡在半途 */
            return 0xFFFF;
        }
    }
    s_last_guard = guard;
    ADC_CONTR &= (uint8_t)~0x20;    /* 写 0 清 ADC_FLAG */

    v = (uint16_t)(((uint16_t)(ADC_RES & 0x0F) << 8) | ADC_RESL);
    return v;
}

uint16_t MQ_Read(void)
{
#if CFG_MQ_DIGITAL || CFG_MQ_AUTO_DO
    if (s_digital != 0)
    {
        /* 数字 DO 模式：把比较器输出映射成 0 / 满量程，
         * 后面的阈值比较、蜂鸣器、显示逻辑一行都不用改。 */
        uint8_t lv = (MQ_ADC_PIN != 0) ? 1 : 0;

#if CFG_MQ_DO_ACTIVE_LOW
        lv = (uint8_t)(lv ^ 1);
#endif
        s_last_to = 0;
        return (lv != 0) ? CFG_TH_MAX : 0;
    }
#endif

    {
        uint16_t sum = 0;
        uint16_t v;
        uint8_t i;
        uint8_t n = 0;
        uint8_t to = 0;

        if (s_present == 0)
        {
            s_last_to = 8;
            return 0xFFFF;          /* ADC 不可用（且没开自动降级），不必每次空转 8×4ms */
        }

        (void)ADC_RawRead(s_ch);    /* 换通道后第一次转换不准，丢掉 */

        for (i = 0; i < 8; i++)
        {
            v = ADC_RawRead(s_ch);
            if (v == 0xFFFF)
            {
                to++;
                continue;
            }
            sum += v;
            n++;
        }

        s_last_to = to;

        if (n == 0)
        {
            return 0xFFFF;          /* 一次都没成功 = ADC 没工作 */
        }
        return (uint16_t)(sum / n);
    }
}
