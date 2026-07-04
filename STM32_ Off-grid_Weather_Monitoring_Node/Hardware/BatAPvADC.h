#ifndef __BATAPVADC_H__
#define __BATAPVADC_H__

#include "stm32f10x.h"

void BatAPvADC_Init(void);
uint16_t BatAPvADC_ReadBatVoltage(void);
uint16_t BatAPvADC_ReadPVVoltage(void);

#endif
