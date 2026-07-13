#include "Delay.h"
#include <intrins.h>

static unsigned char xdata i, j;

void Delay1ms(void) //@11.0592MHz
{
  _nop_();
  i = 2;
  j = 199;
  do {
    while (--j)
      ;
  } while (--i);
}

void Delay_ms(uint16_t ms) {
  while (ms--) {
    Delay1ms();
  }
}