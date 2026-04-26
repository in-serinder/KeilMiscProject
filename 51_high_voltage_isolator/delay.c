#include "delay.h"
#include <INTRINS.H>

void Delay_us(uint8_t t)
{
    while (t--)
        ;
}

void Delay1000us(void) //@11.0592MHz
{
    uint8_t data i, j;

    _nop_();
    i = 2;
    j = 199;
    do
    {
        while (--j)
            ;
    } while (--i);
}



void Delay_ms(uint8_t i) 
{
    while (i)
    {
        Delay1000us();
        i--;
    }
}

void Delay_s(unsigned int i) //@12.000MHz
{
    while (i)
    {
        Delay_ms(1000);
        i--;
    }
}