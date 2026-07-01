#ifndef __PERIPHERAL_H__
#define __PERIPHERAL_H__

#include "stc89c52.h"




void setBuzzerDutyCycle(uint8_t duty);
void BuzzerPWM(uint8_t freq);

void shellFAN(bit state);
void extFAN(bit state);
void heartFAN(bit state);

void runStateLED(bit state);

#endif
