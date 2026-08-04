#ifndef __JUMPER_H__
#define __JUMPER_H__
#include "stm32f10x.h"
#include <stdbool.h>

#define JUMPER_PIN GPIO_Pin_11
#define JUMPER_PORT GPIOB

void Jumper_Init(void);
bool Jumper_IsNightAutoTurnOnLight(void);
bool Jumper_IsOLEDPowerOn(void);

#endif
