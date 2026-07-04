#ifndef __LIGHTLED_CTL_H__
#define __LIGHTLED_CTL_H__
#include "stm32f10x.h"

// 只是控制oled屏幕的供电 不负责驱动oled屏幕
void LightLED_CTL_Init(void);
void LightLED_CTL_On(void);
void LightLED_CTL_Off(void);
void LightLED_CTL_HardSetSync(void);
#endif
