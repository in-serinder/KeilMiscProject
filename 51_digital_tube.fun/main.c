#include "TM1637.h"
#include <Timer.h>
#include <Max7219.h>

int main()
{
  unsigned char i;
  unsigned char dat[8] = {1, 3, 4, 5, 5, 6, 3, 4};
  bit colonState = 1;
  // TM1637_Init();
  Max7219_Init();
  while (1)
  {
    // TM1637_Display4Num(1234, 1);
    // Delay_ms(1000);
    // TM1637_Display4Num(1234, 0);
    for (i = 0; i < 8; i++)
    {
      Max7219_Display(i + 1, dat[i], 1);
    }

    Delay_ms(1000);
  }
}