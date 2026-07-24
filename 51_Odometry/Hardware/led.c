#include "led.h"


void LED_Init(void)
{
    SM_MODE_LED = 1;
    OBOMETRY_MODE_LED = 1;
}

void LED_SM_Mode(void)
{
    SM_MODE_LED = 0;
    OBOMETRY_MODE_LED = 1;
}
void LED_Obometry_Mode(void)
{
    SM_MODE_LED = 1;
    OBOMETRY_MODE_LED = 0;
}