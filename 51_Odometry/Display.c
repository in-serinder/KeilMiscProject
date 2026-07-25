#include "Display.h"


void Display_Init(void)
{
    Max7219_Init();
}


void Display_SM_Mode(float speed, float mil){
    uint8_t format = 2;
    if (speed < 0.5) format = 3;
    LED_SM_Mode();
    Max7219_DisplayFloatSplit(speed, mil, format);
}


void Display_Obometry_Mode( float mil){
    LED_Obometry_Mode();
    Max7219_DisplayFloat(mil, 4);
}