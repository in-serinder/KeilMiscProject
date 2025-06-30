#ifndef __MAX7219_H__
#define __MAX7219_H__

#include <reg52.h>
#include <intrins.h>

#define MAX7219_CLK P1_0
#define MAX7219_DIN P1_1
#define MAX7219_LOAD P1_2

#define MAX7219_DECODEMODE 0x09
#define MAX7219_INTENSITY 0x0A
#define MAX7219_SCANLIMIT 0x0B
#define MAX7219_SHUTDOWN 0x0C
#define MAX7219_DISPLAYTEST 0x0F

void Max7219_Init();
void Max7219_Write(unsigned char address, unsigned char dat);
void Max7219_Display(unsigned char digit, unsigned char value, unsigned char dot_v);

#endif
