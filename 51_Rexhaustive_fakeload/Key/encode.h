// #include "main.c" //回调使用
#include "Delay.h"
#include "stc89c52.h"

#ifndef _ENCODE_H_
#define _ENCODE_H_

void EC11_Init(void);
bit EC11_ScanKey(void);
bit EC11_GetEvent(unsigned char *dir, unsigned char *key);
extern void encode_CallBack(bit dir, bit event);
extern uint8_t idata enc_int_count;
#endif