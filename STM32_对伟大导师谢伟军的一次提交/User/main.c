#include "stm32f10x.h"
#include "Serial.h"
#include "adc.h"
#include "led.h"
#include "Delay.h"

uint8_t warn_flag =0;
void init_key(void);

int main(void)
{

	Serial_init();
	LED_init();
	ADC_init();
	init_key();

	Serial_sendStr("Start");
	while (1)
	{
		Delay_s(1);
		float voltage = ADC_GetVoltage();
		Serial_sendStr(ADC_GetVoltage_byStr());

		if (voltage > 2)
		{
			openCurtain();
			startWarning();
		}
		else
		{
			if (voltage < 1)
			{
				closeCurtain();
			}
		}

		if (warn_flag!=0)
		{
			startWarning();
		}
		else
		{
			Serial_sendStr("No Warning");
			stopWarning();
		}
		
	}
}

// 按键相关直接写在 main.c

void init_key(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	EXTI_InitTypeDef EXTI_InitStruct;
	NVIC_InitTypeDef NVIC_InitStruct;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU; // 上拉输入
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource6); 
	EXTI_InitStruct.EXTI_Line = EXTI_Line6;
	EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;	 
	EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling; 

	EXTI_InitStruct.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStruct);

	NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;		   
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStruct);
}


void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line6) != RESET)
    {
        Delay_ms(10); 

        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0)
        {
            warn_flag = 0;
	
				stopWarning();
			
            
        }
        EXTI_ClearITPendingBit(EXTI_Line6);
    }
}