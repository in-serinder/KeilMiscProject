#include "delay.h"


void Delay1ms(void)	//@22.1184MHz
{
	unsigned char data i, j;

	_nop_();
	i = 4;
	j = 146;
	do
	{
		while (--j);
	} while (--i);
}


void delay_ms(uint16_t ms)
{
    while(ms--)
    {
        Delay1ms();
    }
}


void delay_S(uint16_t s)
{
    
  while(s--)
  {
    Delay1ms();
  }
}
