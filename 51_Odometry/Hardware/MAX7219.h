#ifndef __MAX7219_H__
#define __MAX7219_H__

#include "stc89c52.h"

#define MAX7219_CLK  P23
#define MAX7219_DIN  P24
#define MAX7219_LOAD P22

#define SEG_S     0x5B
#define SEG_U     0x43
#define SEG_C     0x4E
#define SEG_E     0x4F
#define SEG_D     0x7D
#define SEG_BLANK 0x00

void Max7219_Init(void);
void Max7219_Write(uint8_t address, uint8_t dat);
void Max7219_Display(uint8_t digit, uint8_t value, uint8_t dot_v);
void Max7219_DisplayBlank(uint8_t digit, uint8_t dot_v);
void Max7219_Clear(void);
void Max7219_NumDisplay(double num);
void Max7219DateDisplay(char *datestr);
// void Max7219TemperatureAndHumitidyDisplay(uint8_t *temp, uint8_t *hum);
void Max7219_DisplayFloat(float value, uint8_t format);
void Max7219_DisplayFloatSplit(float value1, float value2, uint8_t format);
void Max7219_DisplayDualFmt(float left_val, uint8_t left_fmt,
                           float right_val, uint8_t right_fmt);
void Max7219_DisplayCustomSegments(const uint8_t *segs, uint8_t len);

#endif