#include "stc15w.h"

void Delay1000ms(void) //@11.0592MHz
{
    unsigned char data i, j, k;

    _nop_();
    _nop_();
    i = 43;
    j = 6;
    k = 203;
    do
    {
        do
        {
            while (--k)
                ;
        } while (--j);
    } while (--i);
}

void Delay1ms(void) //@11.0592MHz
{
    unsigned char data i, j;

    _nop_();
    _nop_();
    _nop_();
    i = 11;
    j = 190;
    do
    {
        while (--j)
            ;
    } while (--i);
}

void Delay1us(void) //@11.0592MHz
{
    _nop_();
    _nop_();
    _nop_();
}
