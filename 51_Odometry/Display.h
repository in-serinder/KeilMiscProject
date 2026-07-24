#ifndef __DISPLAY_H__
#define __DISPLAY_H__
#include "stc89c52.h"
#include "Delay.h"
#include "uart.h"
// #include "24c64.h"
#include "MAX7219.h"
#include "led.h"

void Display_Init(void);
void Display_SM_Mode(float speed, float mil);
void Display_Obometry_Mode( float mil);

#endif
// __DISPLAY_H__
