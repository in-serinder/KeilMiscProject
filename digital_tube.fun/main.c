#include "TM1637.h"
#include <Timer.h>

int main()
{
    bit colonState = 1;
    TM1637_Init();
    while (1)
    {
        TM1637_Display4Num(1234, 1);
        Delay_ms(1000);
        TM1637_Display4Num(1234, 0);
        Delay_ms(1000);
    }
}