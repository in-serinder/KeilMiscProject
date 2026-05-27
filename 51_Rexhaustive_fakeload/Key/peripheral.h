#ifndef __PERIPHERAL_H__
#define __PERIPHERAL_H__

#include "stc89c52.h"

typedef unsigned char uint8_t;
typedef unsigned int uint16_t;


void setBuzzerDutyCycle(uint8_t duty);
void BuzzerPWM(uint8_t freq);

void shellFAN(bit state);
void extFAN(bit state);
void heartFAN(bit state);

void runStateLED(bit state);

#endif
