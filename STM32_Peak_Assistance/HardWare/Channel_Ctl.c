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

// PB0=0(NC)=高压继电器, PB0=1(NO)=低压继电器
void ChannelSet_Route_HV(void) { ROUTE_NC(); }

void ChannelSet_Route_LV(void) { ROUTE_NO(); }

// PA7=0(NC)=600V, PA7=1(NO)=250V
void ChannelSet_HV_600V(void) { HV_ROUTE_NC(); }

void ChannelSet_HV_250V(void) { HV_ROUTE_NO(); }

// PB1=0(NC)=36V, PB1=1(NO)=5V
void ChannelSet_LV_36V(void) { LV_ROUTE_NC(); }

void ChannelSet_LV_5V(void) { LV_ROUTE_NO(); }

// 600V通道: PB0=0(高压继电器) + PA7=0(600V)
void ChannelSet_600V(void) {
  ROUTE_NC();
  HV_ROUTE_NC();
}

// 250V通道: PB0=0(高压继电器) + PA7=1(250V)
void ChannelSet_250V(void) {
  ROUTE_NC();
  HV_ROUTE_NO();
}

// 36V通道: PB0=1(低压继电器) + PB1=0(36V)
void ChannelSet_36V(void) {
  ROUTE_NO();
  LV_ROUTE_NC();
}

// 5V通道: PB0=1(低压继电器) + PB1=1(5V)
void ChannelSet_5V(void) {
  ROUTE_NO();
  LV_ROUTE_NO();
}