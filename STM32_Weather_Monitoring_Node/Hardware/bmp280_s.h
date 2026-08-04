#ifndef __BMP280_S_H__
#define __BMP280_S_H__
#include "stm32f10x.h"
#include <stdbool.h>


void BMP280_Sensor_Init(void);
void BMP280_Sensor_Read(void);
float BMP280_Sensor_ReadPressure(void);
float BMP280_Sensor_ReadTemperature(void);



#endif
