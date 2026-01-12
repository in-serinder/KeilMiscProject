#include "stc15w.h"

sbit Charge6VCTL = P1 ^ 5;
sbit Charge4VCTL = P5 ^ 4;

void InitP0(void)
{
    P1M0 |= 0x04 | 0x02;
    P1M1 &= ~(0x04 | 0x20);

    P5M0 |= 0x10;
    P5M1 &= ~0x10;
}

void startCharge6VBAT(void)
{
    Charge6VCTL = 1;
}

void stopCharge6VBAT(void)
{
    Charge6VCTL = 0;
}

void startCharge4VBAT(void)
{
    Charge4VCTL = 1;
}

void stopCharge4VBAT(void)
{
    Charge4VCTL = 0;
}