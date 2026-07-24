#ifndef __MAX7219_H__
#define __MAX7219_H__

#include "stc89c52.h"

void Max7219_Init();
void Max7219_Write(uint8_t address, uint8_t dat);
void Max7219_Display(uint8_t digit, uint8_t value, uint8_t dot_v);
void Max7219_DisplayBlank(uint8_t digit, uint8_t dot_v);
void Max7219_Clear();
void Max7219_NumDisplay(double num);
void Max7219DateDisplay(char *datestr);
// void Max7219TemperatureAndHumitidyDisplay(uint8_t *temp, uint8_t *hum);
void Max7219_DisplayFloat(float value, uint8_t format);
void Max7219_DisplayFloatSplit(float value1, float value2, uint8_t format);

#define MAX7219_CLK P23
#define MAX7219_DIN P24
#define MAX7219_LOAD P22

// sbit MAX7219_CLK  = P2 ^ 0;
// sbit MAX7219_DIN  = P2 ^ 1;
// sbit MAX7219_LOAD = P2 ^ 2;

#endif