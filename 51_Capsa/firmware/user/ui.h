#ifndef __UI_H__
#define __UI_H__

/* =====================================================================
 * ui.h — OLED 显示（128×32，四行 8×8 文本）
 *
 *   页 0 : 任务状态   例：Read 0x50        45%
 *   页 1 : 进度条 —— ★ 固定显示存储卡容量占用（used/total），
 *          不随读写任务的百分比变化；任务进度只体现在页 0 的百分比。
 *          写入完成后上位机经 UG 上报新占用，进度条随之增长。
 *   页 2 : 容量       例：12.3/64.0KB    19.2%
 *   页 3 : 附加信息   例：24C128 x8
 *
 * 刷新策略：
 *   · 驱动层按页差分（内容没变的页一个字节都不发）；
 *   · 状态行按 2% 步进节流（软 I2C 一页约 6ms，不能每包都刷）；
 *   · 进度条只在容量占用变化（UG / 初始化）时重绘。
 * ===================================================================== */

#include "board.h"

void UI_Init(void);
void UI_Boot(bit iic2_pins_ok, uint8_t scan_mask);       /* 上电自检结果 */
void UI_Idle(void);                                     /* 空闲态 */
void UI_Task(bit is_read, uint8_t chip7, uint8_t percent);   /* 读写进行中 */
void UI_Message(const char *line16);                    /* 状态行显示一段文字 */
void UI_Capacity(uint32_t usedBytes, uint32_t totalBytes);   /* 只刷容量行 */
void UI_Progress(uint8_t percent);                      /* 只刷进度条 */

/* 元数据变化后刷新型号行 + 容量行（SET 成功、型号切换时调用）*/
void UI_RefreshMeta(void);

/* 存储卡在位状态（主循环周期探测 24C02 应答，状态翻转时调用）*/
void UI_CardState(bit present);

/* 卡在位但未初始化（24C02 可通讯、属性不完整/非法）*/
void UI_Uninit(void);

/* ★ 属性显示总闸：0 = 未插卡/未初始化，所有容量、型号、进度条绘制全部拦掉。
 *   由 UI_Boot / UI_CardState / UI_Uninit 自动维护；
 *   SET 成功后由 app.c 显式开闸（因为状态机可能早退不触发刷新）。*/
void UI_SetCardReady(bit ready);

#endif /* __UI_H__ */
