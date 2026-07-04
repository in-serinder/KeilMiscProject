#include "OLED_CTRL.h"

void OLED_CTL_Init(void) {
  // oled供电驱动
  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOB, &GPIO_InitStructure);

  //   跳线设置常态供电
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(GPIOB, &GPIO_InitStructure);
}

void OLED_CTL_On(void) { GPIO_SetBits(GPIOB, GPIO_Pin_3); }

void OLED_CTL_Off(void) { GPIO_ResetBits(GPIOB, GPIO_Pin_3); }

void OLED_CTL_HardSetSync(void) {
  if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15) ==
      Bit_RESET) { // 跳线为低电平时 供电
    OLED_CTL_On();
  } else {
    OLED_CTL_Off();
  }
}
