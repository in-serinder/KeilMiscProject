#include <Timer.h>
#include <INTRINS.H>

void Delay_us(unsigned int t)
{
    while (t--)
        ;
}

void Delay1000us(void) //@12.000MHz
{
    unsigned char data i, j;

    i = 2;
    j = 239;
    do
    {
        while (--j)
            ;
    } while (--i);
}

void Delay1000ms(void) //@12.000MHz
{
    unsigned char data i, j, k;

    _nop_();
    i = 8;
    j = 154;
    k = 122;
    do
    {
        do
        {
            while (--k)
                ;
        } while (--j);
    } while (--i);
}

void Delay_ms(unsigned int i) //@12.000MHz
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
        Delay1000ms();
        i--;
    }
}