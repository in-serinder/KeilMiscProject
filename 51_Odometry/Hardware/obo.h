#ifndef __OBO_H__
#define __OBO_H__
#include "stc89c52.h"
#include "24c64.h"
#include "Delay.h"

#define OBO_PIN         P32       // 霍尔传感器输入引脚 (INT0)
#define OBO_RATE        219       // 27.5寸轮子≈219cm  (inch×2.54×pi) 
                                   // 例：700c公路轮≈210cm, 26"山地≈207cm


#define OBO_ACTIVE_HIGH     0    // 霍尔有效性模式：0=高电平有效, 1=低电平有效 但是如果是高电平触发需要对硬件下拉 拆除现有设计上拉电阻改下拉

#define MILES_ADDR       0x01     // 累计里程 (uint32_t, 4字节 0x01~0x04)
#define EEPROM_TEXT_ADDR 0x10     // 开机次数 (uint8_t, 1字节)

#define EEPROM_SAVE_CYCLES   120   // 每120次刷新周期(约60秒)存一次里程
#define COUNT_SLOW           1     // 1=静止时也定期存, 0=不动就不存
#define COUNT_SLOW_PATIENCE  20    // 静止20个周期(约10秒)也强存一次

#define OBO_DEBOUNCE_TICK   10      // 去抖 (10×10ms=100ms) 支持~76km/h

#define WALKING_SPEED_KMH   5.0f   // 低于此速度视为推行/静止,不计入里程
#define SPEED_ZERO_KMH      1.0f   // 低于此速度显示0.0

#define SPEED_WIN_1S_TICK   100    // 1s 窗口（保留extern引用）


#define MAX_PULSES_PER_WINDOW   10 // 若1秒窗口内脉冲数超过此值，视为噪声/尖峰，该窗口速度强制=0


void OBO_Init(void);
void OBO_Poll(void);
void OBO_SET_Millis(uint32_t ms);

float OBO_GET_Miles(void);
float OBO_GET_SPEED(void);


#endif
