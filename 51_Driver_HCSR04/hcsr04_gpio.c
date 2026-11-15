#include "hcsr04.h"

/* ==========================================================================
 * HC-SR04 驱动 —— 模式 0：Trig / Echo
 * --------------------------------------------------------------------------
 * P20 = TRIG（输出，>=10us 高电平触发）
 * P21 = ECHO（输入，高电平宽度正比于距离）
 *
 * 时序换算：
 *   12T 模式下一个机器周期 = 12 / 11.0592MHz = 1.08507us
 *   声速 343m/s @20℃ = 0.343mm/us，收发各走一半
 *   1 计数 = 1.08507 * 0.343 / 2 = 0.18609 mm  =>  mm = counts * 186 / 1000
 *
 * 计时用 Timer0（Timer1 被 9600 波特率占用，不能动）：
 *   16 位最多 65535 计数 = 71ms，够 12m 量程，4.5m 的模块绝不会溢出。
 *
 * 只有 HCSR04_MODE == HCSR04_MODE_TRIG_ECHO 时本文件才产生代码，
 * 所以三个实现文件可以同时放在工程里，靠一个宏切换。
 * ========================================================================== */

#if HCSR04_MODE == HCSR04_MODE_TRIG_ECHO

static unsigned int s_mm;
static unsigned int s_raw;
static unsigned char s_err;

unsigned int HCSR04_MM(void)
{
    return s_mm;
}

unsigned int HCSR04_CM(void)
{
    return (unsigned int)((s_mm + 5) / 10); /* 四舍五入 */
}

unsigned char HCSR04_Error(void)
{
    return s_err;
}

unsigned int HCSR04_Raw(void)
{
    return s_raw;
}

static void t0_start(void)
{
    TMOD = (TMOD & 0xF0) | 0x01; /* Timer0 方式 1：16 位；保留 Timer1 的串口设置 */
    TR0 = 0;
    TH0 = 0x00;
    TL0 = 0x00;
    TF0 = 0;
    TR0 = 1;
}

static unsigned int t0_stop(void)
{
    unsigned int t;

    TR0 = 0;
    t = ((unsigned int)TH0 << 8) | TL0;
    return t;
}

void HCSR04_Init(void)
{
    HCSR04_TRIG = 0;
    HCSR04_ECHO = 1; /* 准双向口：写 1 才能读 */
    s_mm = 0;
    s_raw = 0;
    s_err = HCSR04_OK;
}

bit HCSR04_Measure(void)
{
    unsigned int t, w;

    s_err = HCSR04_OK;
    s_mm = 0;
    s_raw = 0;

    /* 1) 触发脉冲：手册要求 >=10us，这里约 15us */
    HCSR04_TRIG = 1;
    {
        unsigned char i = 7;
        while (i--)
            _nop_();
    }
    HCSR04_TRIG = 0;

    /* 2) 等 ECHO 拉高。模块要先发完 8 个 40kHz 脉冲才回声，
          正常几毫秒内出现；这里约 25ms 超时 */
    w = 0;
    while (HCSR04_ECHO == 0)
    {
        if (++w > 3000)
        {
            s_err = HCSR04_ERR_NO_ECHO;
            return 0;
        }
    }

    /* 3) 量高电平宽度 */
    t0_start();
    while (HCSR04_ECHO)
    {
        if (TF0)
        {
            TF0 = 0;
            TR0 = 0;
            s_err = HCSR04_ERR_OVERFLOW;
            return 0;
        }
    }
    t = t0_stop();

    /* 4) 换算成毫米 */
    s_raw = t;
    s_mm = (unsigned int)(((unsigned long)t * HCSR04_MM_NUM) / HCSR04_MM_DEN);
    if (s_mm == 0)
        s_mm = 1; /* 贴太近时计数值很小，返回 0 会被上层当成失败 */
    return 1;
}

#endif /* HCSR04_MODE_TRIG_ECHO */
