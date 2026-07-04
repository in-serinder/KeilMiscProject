#include "Buzzer.h"

void Buzzer_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void Buzzer_On(void) { GPIO_SetBits(GPIOA, GPIO_Pin_8); }

void Buzzer_Off(void) { GPIO_ResetBits(GPIOA, GPIO_Pin_8); }
