#include "led.h"
#include "stm32f10x.h"

#define Curtain_OPEN_LED(x)  ((x) ? GPIO_SetBits(GPIOA, GPIO_Pin_5) : GPIO_ResetBits(GPIOA, GPIO_Pin_5))
#define Curtain_CLOSE_LED(x) ((x) ? GPIO_SetBits(GPIOA, GPIO_Pin_6) : GPIO_ResetBits(GPIOA, GPIO_Pin_6))
#define WARNING_LED(x)       ((x) ? GPIO_SetBits(GPIOA, GPIO_Pin_7) : GPIO_ResetBits(GPIOA, GPIO_Pin_7))
void LED_init(void)
{

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void openCurtain(void)
{
    Curtain_OPEN_LED(0);
    Curtain_CLOSE_LED(1);
}

void closeCurtain(void)
{
    Curtain_OPEN_LED(1);
    Curtain_CLOSE_LED(0);
}

void stopWarning(void)
{
    WARNING_LED(0);
}

void startWarning(void)
{
    WARNING_LED(1);
}