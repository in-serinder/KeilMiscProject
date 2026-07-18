#ifndef __TM1637_H__
#define __TM1637_H__

#include "stm32f10x.h"

void TM1637_Init(void);
void TM1637_Display(uint8_t pos, uint8_t num, uint8_t colon);
void TM1637_Display4Num(uint16_t num, uint8_t colon);
void TM1637_DisplayRaw(uint8_t pos, uint8_t seg_data);
void TM1637_Clear(void);
void TM1637_ClearPos(uint8_t pos);
void TM1637_SetColon(uint8_t pos, uint8_t on);
void TM1637_ToggleColon(uint8_t pos);

#endif