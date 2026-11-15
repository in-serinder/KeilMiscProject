#include "hcsr04.h"

/* ==========================================================================
 * HC-SR04 驱动 —— 模式 2：UART
 * --------------------------------------------------------------------------
 * 模块拨码：M1 = 1，M2 = 1
 * P20 = RX（模块 TX -> 单片机）
 * P21 = TX（单片机 -> 模块 RX）
 *
 * ★ 关键：STC89C52 的硬件 UART 固定在 P3.0(RXD) / P3.1(TXD)，
 *   P2.0 / P2.1 上没有任何串口复用，所以这里只能用**软件模拟 9600 8N1**。
 *
 *   位宽 = 1/9600 = 104.17us，11.0592MHz 下一个机器周期 1.08507us
 *        => 104.17 / 1.08507 ≈ 96 个机器周期
 *   用 "循环 + _nop_()" 凑：一轮约 3 个周期，取 HCSR04_SWUART_LOOPS ≈ 30。
 *   若读回的值乱跳，用示波器量波形再微调这个宏（每改 1 约 ±3%）。
 *
 * 协议：发 0x55 触发，模块回 2 字节，高字节在前，一般直接是毫米。
 *
 * 只有 HCSR04_MODE == HCSR04_MODE_UART 时本文件才产生代码。
 * ========================================================================== */

#if HCSR04_MODE == HCSR04_MODE_UART

static unsigned int s_mm;
static unsigned int s_raw;
static unsigned char s_err;

unsigned int HCSR04_MM(void)
{
    return s_mm;
}

unsigned int HCSR04_CM(void)
{
    return (unsigned int)((s_mm + 5) / 10);
}

unsigned char HCSR04_Error(void)
{
    return s_err;
}

unsigned int HCSR04_Raw(void)
{
    return s_raw;
}

/* ---------- 软件串口原语 ---------- */

static void swu_delay(void)
{
    unsigned char i = HCSR04_SWUART_LOOPS;
    while (i--)
        _nop_();
}

static void swu_half_delay(void)
{
    unsigned char i = HCSR04_SWUART_LOOPS / 2;
    while (i--)
        _nop_();
}

static void swu_send(unsigned char dat)
{
    unsigned char i;

    HCSR04_TX = 0; /* 起始位 */
    swu_delay();

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x01)
            HCSR04_TX = 1;
        else
            HCSR04_TX = 0;
        dat >>= 1;
        swu_delay();
    }

    HCSR04_TX = 1; /* 停止位 */
    swu_delay();
}

/* 收一个字节；返回 0 = 等不到起始位（超时） */
static bit swu_recv(unsigned char *p)
{
    unsigned char i;
    unsigned int w = 0;

    *p = 0x00;

    while (HCSR04_RX) /* 等起始位下降沿 */
    {
        if (++w > 6000)
            return 0;
    }

    swu_half_delay(); /* 到起始位中心 */
    swu_delay();      /* 再半个位 */
    swu_delay();      /* 采样点落在 bit0 中心附近 */

    for (i = 0; i < 8; i++)
    {
        *p >>= 1;
        if (HCSR04_RX)
            *p |= 0x80;
        swu_delay();
    }

    return 1; /* 停止位不再校验，够用了 */
}

/* ---------- 对外 API ---------- */

void HCSR04_Init(void)
{
    HCSR04_TX = 1; /* 空闲高 */
    HCSR04_RX = 1; /* 输入上拉 */
    s_mm = 0;
    s_raw = 0;
    s_err = HCSR04_OK;
}

bit HCSR04_Measure(void)
{
    unsigned char hi, lo;

    s_err = HCSR04_OK;
    s_mm = 0;
    s_raw = 0;

    swu_send(0x55); /* 通用触发字节 */

    if (!swu_recv(&hi))
    {
        s_err = HCSR04_ERR_TIMEOUT;
        return 0;
    }
    if (!swu_recv(&lo))
    {
        s_err = HCSR04_ERR_TIMEOUT;
        return 0;
    }

    s_raw = ((unsigned int)hi << 8) | lo;
    s_mm = s_raw; /* 模块通常直接回毫米 */
    return 1;
}

#endif /* HCSR04_MODE_UART */
