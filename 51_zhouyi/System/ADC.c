#include "stc15w.h"
#include "ADC.h"
#include "Delay.h"

	static uint16_t rand_seed = 0x1234;

void initADC()
{

    ADC_CONTR &= ~ADCFLAG;
    P1ASF = 0x01; 
    ADC_RES = 0;            
    ADC_RESL = 0;            ; 
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL | ADC_START;
    Delay1ms();
}

uint16_t getADC(void)
{
    unsigned int adc_value = 0;
    ADC_CONTR |= ADC_START;  
    while(!(ADC_CONTR & ADCFLAG));  
    ADC_CONTR &= ~ADCFLAG;  
    adc_value = (ADC_RES << 2) | ADC_RESL;  
    
    return adc_value;
}

uint8_t getRandomNum(){

	    rand_seed = (rand_seed << 1) ^ ((rand_seed & 0x8000) ? 0x1021 : 0);

	return getADC()+rand_seed%8;
}