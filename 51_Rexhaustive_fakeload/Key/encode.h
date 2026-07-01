// #include "main.c" //回调使用
#include "Delay.h"
#include "stc89c52.h"

#ifndef _ENCODE_H_
#define _ENCODE_H_

void EC11_Init(void);
bit checkDirection(void);
bit EC11_ScanKey(void);
extern void encode_CallBack(bit dir, bit event);
#endif