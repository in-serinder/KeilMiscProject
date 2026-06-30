#include "stc89c52.h"

#ifndef _ENCODE_H_
#define _ENCODE_H_

void EC11_Init(void);
bit checkDirection(void);
extern void encode_CallBack(bit dir, bit event);
#endif