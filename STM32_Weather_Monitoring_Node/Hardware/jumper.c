#include "jumper.h"

#define JUMPER_NIGHT_AUTO_TURN_ON_LIGHT_PIN GPIO_Pin_8
#define JUMPER_OLEDP_POWER_ON_PIN GPIO_Pin_9
#define JUMPER_PORT GPIOB

void Jumper_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = JUMPER_NIGHT_AUTO_TURN_ON_LIGHT_PIN | JUMPER_OLEDP_POWER_ON_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(JUMPER_PORT, &GPIO_InitStructure);
}

bool Jumper_IsNightAutoTurnOnLight(void)
{
    return GPIO_ReadInputDataBit(JUMPER_PORT, JUMPER_NIGHT_AUTO_TURN_ON_LIGHT_PIN) == 0;
}

bool Jumper_IsOLEDPowerOn(void)
{
    return GPIO_ReadInputDataBit(JUMPER_PORT, JUMPER_OLEDP_POWER_ON_PIN) == 0;
}