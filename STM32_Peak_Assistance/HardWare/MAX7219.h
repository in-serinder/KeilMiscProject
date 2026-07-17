#ifndef __MAX7219_H__
#define __MAX7219_H__

#include "stm32f10x.h"

#define MAX7219_DECODEMODE 0x09
#define MAX7219_INTENSITY 0x0A
#define MAX7219_SCANLIMIT 0x0B
#define MAX7219_SHUTDOWN 0x0C
#define MAX7219_DISPLAYTEST 0x0F

void MAX7219_Init(void);
void MAX7219_Write(uint8_t address, uint8_t dat);
void MAX7219_Display(uint8_t digit, uint8_t value, uint8_t dot_v);
void MAX7219_Clear(void);

#endif