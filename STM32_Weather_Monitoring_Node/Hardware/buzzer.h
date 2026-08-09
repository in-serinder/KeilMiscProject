#ifndef __BUZZER_H__
#define __BUZZER_H__
#include "stm32f10x.h"
#include <stdbool.h>

void Buzzer_Init(void);
void Buzzer_Config(bool state); /* 0：警报(扫频双音6次)  1：就绪(2kHz短音) */
void Buzzer_On(void);           /* PWM 3000Hz 持续响 (无源蜂鸣器谐振峰值附近) */
void Buzzer_Off(void);          /* 停止 PWM */

#endif