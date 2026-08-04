#ifndef __WEATHER_PREDICTION_H__
#define __WEATHER_PREDICTION_H_
#include "stm32f10x.h"
#include "bmp280_s.h"
#include <stdbool.h>


typedef enum Weather_Prediction {
  Weather_Prediction_ToSunny = 0,   //转晴天
  Weather_Prediction_ToRainy = 1,   //转降雨刮风
  Weather_Prediction_ToOvercast = 2, //转阴天
  Weather_Prediction_ToThunder = 3, //转雷暴强对流
  Weather_Prediction_Invalid = 4,   //无效预测
} Weather_Prediction;

void Weather_Prediction_Init(void);
void Weather_Prediction_Update(void);
Weather_Prediction Weather_Prediction_Print(void);

#endif