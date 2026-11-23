#ifndef __EEPROM_H__
#define __EEPROM_H__

#include "board.h"

/* 用 STC8 片内 Flash 模拟 EEPROM 保存阈值（掉电不丢）。
 *
 * 记录占 8 字节，放在 CFG_EEPROM_BASE 所在的 512B 扇区里：
 *   [0]=0x5A [1]=0xA5 [2..3]=阈值(小端) [4..5]=阈值取反 [6]=校验和 [7]=0xFF
 * 校验和 + 取反 + 魔数三重校验，任一不符就回落到 board.h 的默认阈值。
 *
 * 写一次要"擦整个扇区 + 写 8 字节"，耗时约 25ms，期间关总中断。
 * 所以只在阈值真的改过、并且退出设置模式时才存，不要每次按键都写。 */

/* 读回阈值。返回 1 = EEPROM 里是有效记录；0 = 空白/损坏，*th 不被修改。 */
uint8_t EE_LoadThreshold(uint16_t *th);

/* 擦 + 写 + 回读校验。返回 1 = 成功。 */
uint8_t EE_SaveThreshold(uint16_t th);

#if CFG_DEBUG_UART
/* 最近一次 IAP 操作的 IAP_CONTR.CMD_FAIL（1 = 硬件报告这次操作失败，
 * 常见原因：地址超出 EEPROM 空间、写入时掉电）。 */
uint8_t EE_LastFail(void);
#endif

#endif /* __EEPROM_H__ */
