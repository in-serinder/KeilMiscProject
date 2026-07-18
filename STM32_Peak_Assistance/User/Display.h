#ifndef __DISPLAY_H__
#define __DISPLAY_H_

#include "stm32f10x.h"

#include "Delay.h"
#include "LED.h"
#include "MAX7219.h"
#include "TM1637.h"

void Display_Init(void);
void Display_Clear(void);
void Display_Channel(uint16_t channel);
void Display_Voltage(double voltage, uint8_t is_mv);
void Display_DigitTest(void);
void Display_QuickSelfTest(uint16_t channel);
void Display_PeakVoltage(double voltage_peak, double voltage_threshold);
void Display_NoInput(void); // 显示 "NON.V" 占位

void Test_MAX7219(void);
void Test_TM1637(void);
void Test_ChannelCtl(void);

#endif