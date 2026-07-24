#ifndef __KEY_H__
#define __KEY_H__

#include "stc89c52.h"

#define Mode_Key P33
#define Reset_Key P34

void Key_Init(void);
bit Key_Scan(void);

#endif
