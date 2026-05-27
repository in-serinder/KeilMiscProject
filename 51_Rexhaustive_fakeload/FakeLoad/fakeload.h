#include "pcf8574.h"
#include "stc89c52.h"

#ifndef __FAKELOAD_H__
#define __FAKELOAD_H__
// 主要是假负载的电阻控制定义
#define R10 PCF8574_P0
#define R20_1 PCF8574_P1
#define R20_2 PCF8574_P2
#define R20_3 PCF8574_P3
#define R20_4 PCF8574_P4
#define R100 PCF8574_P5
// #define R10KR PCF8574_P6 | PCF8574_P7

void FakeLoad_Init(void);
uint8_t FakeLoad_SetPower(float power, float voltage);
void FakeLoad_SetResistance(uint8_t resistance_index);
void FakeLoad_Reset(void);
void FakeLoad_Set(uint8_t port, uint8_t value);
#endif