#ifndef __KEY_H__
#define __KEY_H__

#include "stc89c52.h"

#define Mode_Key     P33
#define Reset_Key    P34

#define KEY_DEBOUNCE_CYCLES   2    // 20ms 模式键去抖
#define KEY_LONGPRESS_CYCLES 30   // 3秒 长按重置
#define KEY_INTERVAL_MS      10   // Key_Scan调用间隔

void Key_Init(void);
bit Key_Scan(void);

#endif