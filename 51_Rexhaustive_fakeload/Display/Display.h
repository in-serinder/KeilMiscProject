#include "stc89c52.h"

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

void Display_Init(void);
void Display_BootMessage(void);
void Display_IdleMessage(void);
// void Display_FakeLoadList(unsigned char *loadList);
void Display_FakeLoad(float power, float resistance, float voltage);
void Display_TimerSetupMessage(uint16_t set_seconds, bit is_stand_mode);
void Display_RunningMessage(uint16_t elapsed_seconds, float loadEff,
                            float voltage, bit is_free_run);
void Display_ErrorMessage(char *message);

#endif