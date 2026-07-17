#include "Channel_Ctl.h"

#define ROUTE_PORT GPIOB
#define ROUTE_PIN GPIO_Pin_0
#define HV_ROUTE_PORT GPIOA
#define HV_ROUTE_PIN GPIO_Pin_7
#define LV_ROUTE_PORT GPIOB
#define LV_ROUTE_PIN GPIO_Pin_1

#define ROUTE_NO() GPIO_SetBits(ROUTE_PORT, ROUTE_PIN)
#define ROUTE_NC() GPIO_ResetBits(ROUTE_PORT, ROUTE_PIN)
#define HV_ROUTE_NO() GPIO_SetBits(HV_ROUTE_PORT, HV_ROUTE_PIN)
#define HV_ROUTE_NC() GPIO_ResetBits(HV_ROUTE_PORT, HV_ROUTE_PIN)
#define LV_ROUTE_NO() GPIO_SetBits(LV_ROUTE_PORT, LV_ROUTE_PIN)
#define LV_ROUTE_NC() GPIO_ResetBits(LV_ROUTE_PORT, LV_ROUTE_PIN)

void ChannelSet_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

  GPIO_InitStructure.GPIO_Pin = ROUTE_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(ROUTE_PORT, &GPIO_InitStructure);
  GPIO_ResetBits(ROUTE_PORT, ROUTE_PIN);

  GPIO_InitStructure.GPIO_Pin = HV_ROUTE_PIN;
  GPIO_Init(HV_ROUTE_PORT, &GPIO_InitStructure);
  GPIO_ResetBits(HV_ROUTE_PORT, HV_ROUTE_PIN);

  GPIO_InitStructure.GPIO_Pin = LV_ROUTE_PIN;
  GPIO_Init(LV_ROUTE_PORT, &GPIO_InitStructure);
  GPIO_ResetBits(LV_ROUTE_PORT, LV_ROUTE_PIN);
}

void ChannelSet_Route_HV(void) { ROUTE_NO(); }

void ChannelSet_Route_LV(void) { ROUTE_NC(); }

void ChannelSet_HV_600V(void) { HV_ROUTE_NO(); }

void ChannelSet_HV_250V(void) { HV_ROUTE_NC(); }

void ChannelSet_LV_36V(void) { LV_ROUTE_NO(); }

void ChannelSet_LV_5V(void) { LV_ROUTE_NC(); }

void ChannelSet_600V(void) {
  ROUTE_NO();
  HV_ROUTE_NO();
}

void ChannelSet_250V(void) {
  ROUTE_NO();
  HV_ROUTE_NC();
}

void ChannelSet_36V(void) {
  ROUTE_NC();
  LV_ROUTE_NO();
}

void ChannelSet_5V(void) {
  ROUTE_NC();
  LV_ROUTE_NC();
}