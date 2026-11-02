#ifndef __DISPLAY_H__
#define __DISPLAY_H__

/* =====================================================================
 * display.h — 数码管显示状态（前两位状态码，后两位 I2C 器件地址）
 * 状态码与模拟器 eeprom_sim/tm1637.py 保持一致，方便和上位机/模拟器对拍。
 * ===================================================================== */

#include "board.h"

void Disp_Init(void);
void Disp_Update(uint8_t state, uint8_t router);

/* 调试用：先读回 CLK/DIO 判断引脚是否受控，再把四位数码管全段点亮约
 * 0.2s 后清屏。返回 1 = 引脚正常（此时肉眼应看到 8.8.8.8）；
 * 返回 0 = 引脚不受控（P5.4 仍是 RST / 接线不对）。 */
bit Disp_SelfTest(void);

#endif /* __DISPLAY_H__ */
