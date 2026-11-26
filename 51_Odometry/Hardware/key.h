#ifndef __KEY_H__
#define __KEY_H__

#include "stc89c52.h"

#define Mode_Key     P33
#define Reset_Key    P34

#define KEY_DEBOUNCE_CYCLES   2    // 20ms 模式键去抖
/* 长按判定按"主循环圈数"计：一圈 ≈ Delay_ms(10) ≈ 10ms。
 * 200 圈 ≈ 2s，既不误触（骑行颠簸/拧钥匙）又不至于按半天没反应。
 * 原来 30 圈只有约 0.3s，一碰就清里程。 */
#define KEY_LONGPRESS_CYCLES 200
#define KEY_INTERVAL_MS      10   // Key_Scan调用间隔

void Key_Init(void);
bit Key_Scan(void);

#endif