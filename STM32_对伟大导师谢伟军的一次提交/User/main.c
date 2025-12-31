#include "stm32f10x.h"
#include "Serial.h"
#include "adc.h"
#include "led.h"
#include "misc.h"
#include "Delay.h"

uint8_t warn_flag = 0;
void init_key(void);

int main(void)
{
    Serial_init();
    LED_init();
    ADC_init();
    init_key();

    Serial_sendStr("Start\r\n");
    while (1)
    {
        Delay_ms(100);
        float voltage = ADC_GetVoltage();
        Serial_sendStr(ADC_GetVoltage_byStr());
        Serial_sendStr("\r\n");
					
			if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_6)==0){
				warn_flag = 0;
			}
			
        if (voltage > 2.0f)
        {
            openCurtain();
            startWarning();
					warn_flag =1;
        }
        else if (voltage < 1.0f)
        {
            closeCurtain();
					warn_flag =1;
        }

        if (warn_flag ==1)
        {
            startWarning();
					Serial_sendStr("Warning\r\n");
        }
        else
        {
            Serial_sendStr("No Warning\r\n");
            stopWarning();
        }
    }
}

void init_key(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6;
		GPIO_InitStruct.GPIO_Mode=GPIO_Mode_IPU; //??
		GPIO_InitStruct.GPIO_Pin=GPIO_Pin_All;
		GPIO_InitStruct.GPIO_Speed=GPIO_Speed_50MHz;
		GPIO_Init(GPIOB,&GPIO_InitStruct);
}
