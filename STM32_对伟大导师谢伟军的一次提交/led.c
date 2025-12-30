#include "led.h"
#include "srm32f10x.h"

#define Curtain_OPEN_LED(x) GPIOWriteBit(GPIOA,GPIO_Pin_5,x)
#define Curtain_CLOSE_LED(x) GPIOWriteBit(GPIOA,GPIO_Pin_6,x)
#define WARNING_LED(x) GPIOWriteBit(GPIOA,GPIO_Pin_7,x)

void LED_init(){

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	GPIO_InitTypeDef GPIO_InitStructure;
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
 	GPIO_Init(GPIOA, &GPIO_InitStructure);
 	
}



void openCurtain(){
    Curtain_OPEN_LED(1);
    Curtain_CLOSE_LED(0);
}

void closeCurtain(){
    Curtain_OPEN_LED(0);
    Curtain_CLOSE_LED(1);
}

void stopWarning(){
    WARNING_LED(0);
}

void startWarning(){
    WARNING_LED(1);
}