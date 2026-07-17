#include "stm32f10x.h"

/*
 * 通道控制头文件
 *
 * 硬件路由表:
 * ┌────────────────────────────────────────────────────────────────┐
 * │ 继电器    │ 引脚  │ 功能                                       │
 * ├───────────┼───────┼────────────────────────────────────────────┤
 * │ ROUTE     │ PB0   │ NO=高压继电器路由    NC=低压继电器路由      │
 * │ HV_ROUTE  │ PA7   │ NO=600V分压通道      NC=250V分压通道        │
 * │ LV_ROUTE  │ PB1   │ NO=36V分压通道       NC=5V分压通道          │
 * └───────────┴───────┴────────────────────────────────────────────┘
 *
 * 完整通道选择组合:
 * ┌──────────────┬──────┬──────┬──────┬──────────────────────────┐
 * │ 目标通道     │ PB0  │ PA7  │ PB1  │ 调用函数                 │
 * ├──────────────┼──────┼──────┼──────┼──────────────────────────┤
 * │ 600V         │ NO(1)│ NO(1)│  x   │ ChannelSet_600V()        │
 * │ 250V         │ NO(1)│ NC(0)│  x   │ ChannelSet_250V()        │
 * │ 36V          │ NC(0)│  x   │ NO(1)│ ChannelSet_36V()         │
 * │ 5V           │ NC(0)│  x   │ NC(0)│ ChannelSet_5V()          │
 * │ 仅切高压侧   │ NO(1)│  x   │  x   │ ChannelSet_Route_HV()    │
 * │ 仅切低压侧   │ NC(0)│  x   │  x   │ ChannelSet_Route_LV()    │
 * │ 仅切600V子路 │  x   │ NO(1)│  x   │ ChannelSet_HV_600V()     │
 * │ 仅切250V子路 │  x   │ NC(0)│  x   │ ChannelSet_HV_250V()     │
 * │ 仅切36V子路  │  x   │  x   │ NO(1)│ ChannelSet_LV_36V()      │
 * │ 仅切5V子路   │  x   │  x   │ NC(0)│ ChannelSet_LV_5V()       │
 * └──────────────┴──────┴──────┴──────┴──────────────────────────┘
 *
 * 注: x=不关心; NO=GPIO置位; NC=GPIO复位
 */
#ifndef __CHANNEL_CTL_H__
#define __CHANNEL_CTL_H_

void ChannelSet_Init(void);
void ChannelSet_Route_HV(void);
void ChannelSet_Route_LV(void);
void ChannelSet_HV_600V(void);
void ChannelSet_HV_250V(void);
void ChannelSet_LV_36V(void);
void ChannelSet_LV_5V(void);
void ChannelSet_600V(void);
void ChannelSet_250V(void);
void ChannelSet_36V(void);
void ChannelSet_5V(void);

#endif