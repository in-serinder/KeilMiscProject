#ifndef __TOGGLE_KEY_H__
#define __TOGGLE_KEY_H__

#include "stm32f10x.h"

typedef enum {
  KEY_mvMode_Pos = 0,
  KEY_PeakMode_Pos = 1,
  KEY_TestMode_Pos = 2,
  KEY_Hold_Pos = 3,
} KeyBitPos_t;

#define KEY_PEAKMODE_PORT GPIOA
#define KEY_PEAKMODE_PIN GPIO_Pin_11
#define KEY_MVMODE_PORT GPIOA
#define KEY_MVMODE_PIN GPIO_Pin_12
#define KEY_TESTMODE_PORT GPIOA
#define KEY_TESTMODE_PIN GPIO_Pin_13
#define KEY_HOLD_PORT GPIOA
#define KEY_HOLD_PIN GPIO_Pin_14

void ToggleKey_Init(void);
uint8_t ToggleKey_GetPeakMode(void);
uint8_t ToggleKey_GetmvMode(void);
uint8_t ToggleKey_GetTestMode(void);
uint8_t ToggleKey_GetHold(void);
uint8_t ToggleKey_GetAll(void);

#endif