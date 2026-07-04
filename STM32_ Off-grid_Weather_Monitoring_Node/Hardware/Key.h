#ifndef __KEY_H__
#define __KEY_H__
#include "stm32f10x.h"
void Key_Init(void);

uint8_t Key_Read_PA15(void);
uint8_t Key_Read_PB5(void);
#endif
