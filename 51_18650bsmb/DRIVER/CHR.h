#include "stc15w.h"

#ifndef __CHR_H__
#define __CHR_H__

void CHR_Init(void);
void CHR_Set_CH1(uint8_t state);
void CHR_Set_CH2(uint8_t state);
void CHR_Set_CH3(uint8_t state);
void CHR_Set_CH4(uint8_t state);
void CHR_Set_CH(uint8_t ch, uint8_t state);

#endif