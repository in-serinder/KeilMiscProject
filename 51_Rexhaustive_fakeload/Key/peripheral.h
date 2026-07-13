#ifndef __PERIPHERAL_H__
#define __PERIPHERAL_H__

#include "stc89c52.h"

// 1ms硬件滴答计数器（由Timer0 ISR维护）
extern volatile uint16_t idata sys_tick_1ms;

void setBuzzerDutyCycle(uint8_t duty);
void BuzzerPWM(uint8_t freq);

void shellFAN(bit state);
void extFAN(bit state);
void heartFAN(bit state);

void runStateLED(bit state);

#endif