#ifndef __LED_H__
#define __LED_H__
#include "stc89c52.h"
#include "Delay.h"

#define SM_MODE_LED P15
#define OBOMETRY_MODE_LED P16

void LED_Init(void);
void LED_SM_Mode(void);
void LED_Obometry_Mode(void);



#endif
// __LED_H__
