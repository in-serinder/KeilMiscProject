#ifndef __WEATHER_PREDICTION_H__
#define __WEATHER_PREDICTION_H_
#include "stm32f10x.h"
#include "bmp280_s.h"
#include <stdbool.h>


typedef enum Weather_Prediction {
  Weather_Prediction_ToSunny = 0,
  Weather_Prediction_ToRainy = 1,
  Weather_Prediction_ToOvercast = 2,
  Weather_Prediction_ToThunder = 3,
  Weather_Prediction_Invalid = 4,
} Weather_Prediction;

void Weather_Prediction_Init(void);
void Weather_Prediction_Update(void);
Weather_Prediction Weather_Prediction_Print(void);
void Weather_Prediction_GetRaw(float *outP, float *outDPShort, float *outDPLong,
                                Weather_Prediction *outCand, Weather_Prediction *outCur,
                                uint8_t *outConfirmCnt, uint8_t *outSampleCnt);

#endif