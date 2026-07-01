#include "Delay.h"
#include <intrins.h>

static unsigned char xdata i, j;
void Delay(unsigned int xms) {
  
  while (xms--) {
    i = 2;
    j = 239;
    do {
      while (--j)
        ;
    } while (--i);
  }
}

void Delay10us() //@11.0592MHz
{


  i = 2;
  while (--i)
    ;
}

void Delay1ms() //@11.0592MHz
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