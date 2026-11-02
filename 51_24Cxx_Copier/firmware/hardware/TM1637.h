#ifndef __TM1637_H__
#define __TM1637_H__

/* =====================================================================
 * TM1637.h — 四位数码管（CLK/DIO 两根线）
 * 引脚在 board.h 里定义（默认 P5.4=CLK, P5.5=DIO）。
 * ===================================================================== */

#include "board.h"

/* 每个时钟/数据位之间的延时单元数。
 *
 * TM1637 手册（开关/时序特性，测试条件 VDD = 4.5 ~ 5.5V）：
 *   Fmax 500kHz（占空比 50%）、PWCLK ≥ 400ns、tSETUP/tHOLD ≥ 100ns、
 *   tWAIT（CLK↑ → 下一个 CLK↓）≥ 1µs
 *
 * Delay_us() 一个单元约 0.65us（Delay1us 本体 7 个指令周期 + 调用/返回开销）。
 * tm_write 每 bit 有 3 次 TM_DLY，而 CLK↑ 到下一次 CLK↓ 之间正好是一个 TM_DLY，
 * 所以 tWAIT ≈ CFG × 0.65us：
 *   CFG = 2  -> tWAIT ≈ 1.3us   只比最小值多一点，边沿稍慢就丢整帧
 *   CFG = 5  -> tWAIT ≈ 3.3us   约 100kHz   <-- 默认，稳
 *   CFG = 10 -> 约 50kHz        长线 / 劣质模块
 *
 * ⚠️ TM1637 的电特性只在 4.5~5.5V 保证。若模块用 5V 而 MCU 的 IO 是 3.3V，
 * 模块的 VIH = 0.7×VDD = 3.5V，3.3V 的高电平它认不出来 —— 表现就是全黑。
 * 模块 VDD 必须和 MCU 的 IO 电平一致（都用 5V 最稳）。
 */
#ifndef CFG_TM1637_DLY_US
#define CFG_TM1637_DLY_US       5
#endif

/* 亮度 0~7（7 最亮） */
#ifndef CFG_TM1637_BRIGHT
#define CFG_TM1637_BRIGHT       7
#endif

void    TM1637_Init(void);
void    TM1637_Clear(void);
void    TM1637_SetBright(uint8_t level);

/* 一次写入 4 个段码（自动地址递增模式，C0H 起） */
void    TM1637_WriteSegs(const uint8_t *seg, uint8_t n);

/* 显示 4 个字符，如 "IDLE" / "WW50"；colon=1 时第 3 位点亮小数点 */
void    TM1637_Show4(const char *s, bit colon);

/* 字符 -> 段码（a=0x01 b=0x02 c=0x04 d=0x08 e=0x10 f=0x20 g=0x40 dp=0x80） */
uint8_t TM1637_CharToSeg(char c);

#endif /* __TM1637_H__ */
