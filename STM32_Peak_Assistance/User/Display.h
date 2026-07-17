#ifndef __DISPLAY_H__
#define __DISPLAY_H_

#include "stm32f10x.h"

#include "Delay.h"
#include "LED.h"
#include "MAX7219.h"
#include "TM1637.h"

void Display_Init(void);
void Display_Clear(void);
void Display_Channel(uint8_t channel);

#endif