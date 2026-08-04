#ifndef __BUZZER_H__
#define __BUZZER_H__
#include "stm32f10x.h"
#include <stdbool.h>

void Buzzer_Init(void);
void Buzzer_Config(bool state); //0：警报挂起 1：警报取消
void Buzzer_On(void);
void Buzzer_Off(void);

#endif