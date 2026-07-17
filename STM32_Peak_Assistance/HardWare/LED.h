#ifndef __LED_H__
#define __LED_H__

#include "stm32f10x.h"

void LED_Init(void);
void LED_RED_On(void);
void LED_RED_Off(void);
void LED_RED_Toggle(void);
void LED_GREEN_On(void);
void LED_GREEN_Off(void);
void LED_GREEN_Toggle(void);

#endif