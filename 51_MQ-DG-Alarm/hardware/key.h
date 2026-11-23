#ifndef __KEY_H__
#define __KEY_H__

#include "board.h"

/* add / sub 两个按键，接在数码管模块的 A-K1 / A-K2 上，
 * 通过 TM1637 片内键扫（42H）读取，不占用额外 IO。
 *
 * 解码方式见 key.c 顶部说明：42H 读回的是低电平有效的单字节，
 * b4=0 就是 K1 行（add）、b3=0 就是 K2 行（sub），与 A 接在哪条 SEG 上无关，
 * 所以这里不需要配置任何位掩码。
 *
 * 取键方式由 board.h 的 CFG_KEY_GPIO_MODE 决定：
 *   0 = 走 TM1637 片内键扫（A 接在模块内部某条 SEG 上）—— 本板默认
 *   1 = 走 GPIO 直读（模块的 A 其实是 GND，K1/K2 直接引到 P3.0/P3.1）
 */

#define KEY_ADD                 0x01
#define KEY_SUB                 0x02

/* 每 10ms 调用一次：读键 + 消抖（连续 2 次一致才认），返回稳定的键状态。 */
uint8_t  Key_Update(void);

#if CFG_KEY_RAW_SHOW || CFG_DEBUG_UART
/* 最近一次读回的原始键值（两个字节拼成 16 位），自检/串口调试用。 */
uint16_t Key_LastRaw(void);
#endif

#endif /* __KEY_H__ */
