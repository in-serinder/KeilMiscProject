#include "Key.h"

void Key_Init(void) {
  // 按键PA15 内部上拉输入
  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  // 按键PB5 内部上拉输入
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(GPIOB, &GPIO_InitStructure);
}

uint8_t Key_Read_PA15(void) {
  return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15);
}
uint8_t Key_Read_PB5(void) { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5); }