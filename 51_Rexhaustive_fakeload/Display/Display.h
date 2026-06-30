#include "stc89c52.h"

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

void Display_Init(void);
void Display_BootMessage(void);
// void Display_FakeLoadList(unsigned char *loadList);
void Display_FakeLoad(float power);
void Display_TimerSetupMessage(uint16_t set_seconds);
void Display_RunningMessage(uint16_t elapsed_seconds, uint8_t *loadEff,
                            uint8_t *voltage);

#endif