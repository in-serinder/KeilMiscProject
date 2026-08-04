#ifndef __KEY_H__
#define __KEY_H__
#include "stm32f10x.h"

enum KeyState{
    Key_None = -1,
    Key_LIGHT_Power = 0,
    Key_OLED_Power = 1,
};

void Key_Init(void);
enum KeyState Key_Read(void);

#endif