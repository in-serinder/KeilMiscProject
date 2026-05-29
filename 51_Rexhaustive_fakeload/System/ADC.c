#include "ADC.h"

void ADC_Init(void) {
  ADC_CS = 1;
  ADC_CLK = 0;
  ADC_DIN = 0;
  ADC_DOUT = 0;
}