#ifndef __DELAY_H__
#define __DELAY_H__

#include "board.h"

/* 基准延时单元（阻塞，靠空循环计数，精度受编译优化影响很小，
 * 但会随主频变化，换主频务必重新标定） */
void Delay1us(void);            /* @22.1184MHz 短延时（约 0.4us，用作位延时单元） */
void Delay1ms(void);            /* @22.1184MHz 约 1ms */

void delay_us(uint16_t us);
void delay_ms(uint16_t ms);

/* 兼容旧代码里的命名 */
#define Delay_us(u)     delay_us(u)
#define Delay_ms(m)     delay_ms(m)
#define DelayMs(m)      delay_ms(m)
#define DelayUs(u)      delay_us(u)

#endif /* __DELAY_H__ */
