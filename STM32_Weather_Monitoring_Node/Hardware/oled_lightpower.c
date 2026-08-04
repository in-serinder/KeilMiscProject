#include "oled_lightpower.h"


#define OLED_LIGHTPOWER_PIN GPIO_Pin_3
#define set_OLED_LIGHTPOWER_ON() GPIO_SetBits(GPIOB, OLED_LIGHTPOWER_PIN)
#define set_OLED_LIGHTPOWER_OFF() GPIO_ResetBits(GPIOB, OLED_LIGHTPOWER_PIN)


void Oled_LightPower_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = OLED_LIGHTPOWER_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    set_OLED_LIGHTPOWER_OFF();
}

void Oled_LightPower_On(void)
{
    set_OLED_LIGHTPOWER_ON();
}
void Oled_LightPower_Off(void)
{
    set_OLED_LIGHTPOWER_OFF();
}
