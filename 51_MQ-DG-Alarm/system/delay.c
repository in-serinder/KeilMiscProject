/* delay.c — 软件延时 @ STC8G1K08A 22.1184MHz 1T
 *
 * 说明：
 *   Delay1us 只给 TM1637 这类位操作当"延时单元"用。
 *   按 C51 生成的汇编算：MOV(1) + DJNZ(2) + DJNZ(2) + RET(2) = 7 个时钟，
 *   加上 LCALL/RET 与循环开销，delay_us(1) 约 0.65us（不是 1us，别拿它当精确定时）。
 *   Delay1ms 由 22*128 次空循环构成，22.1184MHz/1T 下接近 1ms；
 *   它只用于 24Cxx 写周期与 2s 协议超时，误差几 % 无影响。
 */
#include "delay.h"

void Delay1us(void)
{
    uint8_t i;

    i = 3;
    while (--i);
}

void Delay1ms(void)
{
    uint8_t i, j;

    _nop_();
    _nop_();
    i = 22;
    j = 128;
    do
    {
        while (--j);
    } while (--i);
}

void delay_us(uint16_t us)
{
    while (us--)
    {
        Delay1us();
    }
}

void delay_ms(uint16_t ms)
{
    while (ms--)
    {
        Delay1ms();
    }
}
