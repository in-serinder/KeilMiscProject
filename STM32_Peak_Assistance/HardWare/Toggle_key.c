#include "Toggle_key.h"

void ToggleKey_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
  GPIO_InitStructure.GPIO_Pin =
      KEY_PEAKMODE_PIN | KEY_MVMODE_PIN | KEY_TESTMODE_PIN | KEY_HOLD_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
}

uint8_t ToggleKey_GetPeakMode(void) {
  return (GPIO_ReadInputDataBit(KEY_PEAKMODE_PORT, KEY_PEAKMODE_PIN) ==
          Bit_RESET)
             ? 1
             : 0;
}

uint8_t ToggleKey_GetmvMode(void) {
  return (GPIO_ReadInputDataBit(KEY_MVMODE_PORT, KEY_MVMODE_PIN) == Bit_RESET)
             ? 1
             : 0;
}

uint8_t ToggleKey_GetTestMode(void) {
  return (GPIO_ReadInputDataBit(KEY_TESTMODE_PORT, KEY_TESTMODE_PIN) ==
          Bit_RESET)
             ? 1
             : 0;
}

uint8_t ToggleKey_GetHold(void) {
  return (GPIO_ReadInputDataBit(KEY_HOLD_PORT, KEY_HOLD_PIN) == Bit_RESET) ? 1
                                                                           : 0;
}

uint8_t ToggleKey_GetAll(void) {
  uint8_t val = 0;
  val |= (ToggleKey_GetmvMode() << KEY_mvMode_Pos);
  val |= (ToggleKey_GetPeakMode() << KEY_PeakMode_Pos);
  val |= (ToggleKey_GetTestMode() << KEY_TestMode_Pos);
  val |= (ToggleKey_GetHold() << KEY_Hold_Pos);
  return val;
}