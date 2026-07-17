#include "LED.h"

#define LED_RED_PORT GPIOB
#define LED_RED_PIN GPIO_Pin_6
#define LED_GREEN_PORT GPIOB
#define LED_GREEN_PIN GPIO_Pin_5

void LED_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

  GPIO_InitStructure.GPIO_Pin = LED_RED_PIN | LED_GREEN_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOB, &GPIO_InitStructure);

  GPIO_SetBits(LED_RED_PORT, LED_RED_PIN);
  GPIO_SetBits(LED_GREEN_PORT, LED_GREEN_PIN);
}

void LED_RED_On(void) { GPIO_ResetBits(LED_RED_PORT, LED_RED_PIN); }

void LED_RED_Off(void) { GPIO_SetBits(LED_RED_PORT, LED_RED_PIN); }

void LED_RED_Toggle(void) {
  if (GPIO_ReadOutputDataBit(LED_RED_PORT, LED_RED_PIN) == Bit_RESET)
    GPIO_SetBits(LED_RED_PORT, LED_RED_PIN);
  else
    GPIO_ResetBits(LED_RED_PORT, LED_RED_PIN);
}

void LED_GREEN_On(void) { GPIO_ResetBits(LED_GREEN_PORT, LED_GREEN_PIN); }

void LED_GREEN_Off(void) { GPIO_SetBits(LED_GREEN_PORT, LED_GREEN_PIN); }

void LED_GREEN_Toggle(void) {
  if (GPIO_ReadOutputDataBit(LED_GREEN_PORT, LED_GREEN_PIN) == Bit_RESET)
    GPIO_SetBits(LED_GREEN_PORT, LED_GREEN_PIN);
  else
    GPIO_ResetBits(LED_GREEN_PORT, LED_GREEN_PIN);
}