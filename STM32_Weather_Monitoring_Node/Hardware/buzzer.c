#include "buzzer.h"
#include "Delay.h"

#define BUZZER_PIN GPIO_Pin_8

static uint16_t buzzer_arr = 0;
static uint16_t buzzer_psc = 0;

static void Buzzer_SetFreq(uint16_t freq)
{
    TIM_Cmd(TIM1, DISABLE);
    buzzer_arr = (uint16_t)(72000000 / freq - 1);
    TIM1->ARR = buzzer_arr;
    TIM1->CCR1 = (buzzer_arr + 1) / 2;
    TIM_Cmd(TIM1, ENABLE);
}

void Buzzer_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = BUZZER_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    TIM_TimeBaseStructure.TIM_Period = 1000 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = 72 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 500;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);

    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_Cmd(TIM1, DISABLE);
}

void Buzzer_On(void)
{
    Buzzer_SetFreq(2500);
}

void Buzzer_Off(void)
{
    TIM_Cmd(TIM1, DISABLE);
}

void Buzzer_Config(bool state)
{
    uint8_t i;
    if (state == 0)
    {
        for (i = 0; i < 6; i++)
        {
            Buzzer_SetFreq(900);
            Delay_ms(150);
            Buzzer_SetFreq(1600);
            Delay_ms(150);
        }
        Buzzer_Off();
    }
    else
    {
        Buzzer_SetFreq(2000);
        Delay_ms(600);
        Buzzer_Off();
    }
}