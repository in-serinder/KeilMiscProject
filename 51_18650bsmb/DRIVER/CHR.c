#include "CHR.h"

#define CHR_CH1 P14
#define CHR_CH2 P15
#define CHR_CH3 P54
#define CHR_CH4 P55

// TP4056 CE低电平启用充电 默认不启用
void CHR_Init(void) {
  CHR_CH1 = 1;
  CHR_CH2 = 1;
  CHR_CH3 = 1;
  CHR_CH4 = 1;
}

void CHR_Set_CH1(uint8_t state) { CHR_CH1 = state; }

void CHR_Set_CH2(uint8_t state) { CHR_CH2 = state; }

void CHR_Set_CH3(uint8_t state) { CHR_CH3 = state; }

void CHR_Set_CH4(uint8_t state) { CHR_CH4 = state; }
