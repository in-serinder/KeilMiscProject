#include "key.h"
#include "Delay.h"

/*
PA15 照明LED按键
PB5 OLED屏幕显示按键
*/

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

uint8_t Key_Read(void)
{
    uint8_t key1 = 0;
    uint8_t key2 = 0;
    // PA15 照明LED按键 处于位 01
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15) == 0)
    {
        Delay_ms(20);
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15) == 0)
        {
            key1 = 1;
            while (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15) == 0);
        }
    }
    // PB5 OLED屏幕显示按键 处于位 10
    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0)
    {
        Delay_ms(20);
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0)
        {
            key2 = 1;
            while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0);
        }
    }
    return (key2 << 1) | key1;
}