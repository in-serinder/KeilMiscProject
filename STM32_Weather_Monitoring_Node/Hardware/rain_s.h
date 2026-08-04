#ifndef __RAIN_S_H__
#define __RAIN_S_H__
#include "stm32f10x.h"
#include <stdbool.h>


#define RAIN_SENSOR_RAIN_THRESHOLD 1000 //1000mv 为雨

void Rain_Sensor_Init(void);
uint16_t Rain_Sensor_Read(void);
bool Rain_Sensor_IsRain(void);



#endif