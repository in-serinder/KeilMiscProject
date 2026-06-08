#include "stc89c52.h"

#ifndef ENCODE_H
#define ENCODE_H

void EC11_Init(void);
bit checkDirection(void);
extern void encode_CallBack(bit dir, bit event);
#endif