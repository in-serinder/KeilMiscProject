#ifndef __DS1302_H__
#define __DS1302_H__

#include "stc89c52.h"

void DS1302_Init(void);
void DS1302_Config(void);
void DS1302_ReadTime(void);
void setNTPTime(unsigned char *time);
void getDS1302Time(unsigned char *time);

#endif