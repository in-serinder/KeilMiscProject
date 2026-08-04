#include "Light_ctl.h"

#define LIGHT_PIN GPIO_Pin_4
#define set_LIGHT_ON() GPIO_SetBits(GPIOB, LIGHT_PIN)
#define set_LIGHT_OFF() GPIO_ResetBits(GPIOB, LIGHT_PIN)

void Light_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    set_LIGHT_OFF();
}


void Light_On(void)
{
    set_LIGHT_ON();
}

void Light_Off(void)
{
    set_LIGHT_OFF();
}