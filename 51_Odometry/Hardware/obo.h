#ifndef __OBO_H__
#define __OBO_H__
#include "stc89c52.h"
#include "24c64.h"
#include "Delay.h"

#define OBO P32

void OBO_Init(void);
void OBO_SET_Millis(uint32_t ms);

float OBO_GET_Miles(void);
float OBO_GET_SPEED(void);


#endif
