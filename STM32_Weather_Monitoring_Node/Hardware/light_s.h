#ifndef __LIGHT_S_H__
#define __LIGHT_S_H__
#include "stm32f10x.h"
#include <stdbool.h>



void Light_Sensor_Init(void);
uint16_t Light_Sensor_Read(void);
bool Light_Sensor_IsNight(void);
float Light_Sensor_ReadVoltage(void);

#endif
