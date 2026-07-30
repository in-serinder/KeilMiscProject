#include "Delay.h"
#include <intrins.h>

static unsigned char xdata i, j;

void Delay1ms(void)	//@11.0592MHz
{
	unsigned char data i, j;

	_nop_();
	_nop_();
	_nop_();
	i = 11;
	j = 190;
	do
	{
		while (--j);
	} while (--i);
}


void Delay_ms(uint16_t ms) {
  while (ms--) {
    Delay1ms();
  }
}

void Delay_us(uint16_t us)	//@12.000MHz 粗略延时（12T下约1.085us/次循环）
{
  while(us--){
	_nop_();
    _nop_();
  }

}