#ifndef __TM1637_H__
#define __TM1637_H__

#include <REG52.H>

// 定义TM1637的时钟和数据引脚
// sbit CLK = P2 ^ 0;
// sbit DIO = P2 ^ 1;

void TM1637_Init(void);
void TM1637_Display(unsigned char pos, unsigned char num, bit colon);
void TM1637_Display4Num(unsigned int num, bit colon);

#endif
