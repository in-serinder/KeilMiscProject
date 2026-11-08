#ifndef __SSD13XX_H__
#define __SSD13XX_H__

/* =====================================================================
 * ssd13xx.h — SSD1315 128×32 OLED（IIC2 软件 I2C）
 *
 * 刻意做成最小实现：
 *   - 只有 8×8 点阵，四页正好四行文本（不再带 8×16 字库，省 1.5 KB Flash）
 *   - 不做通用像素绘图，进度条由 OLED_Bar 直接按整行字节拼出来
 *   - 每次写都是"整行 128 列一次事务"，比逐字符快一个数量级
 *
 * 页分配（每页 8 像素高）：
 *   0  任务状态：Read 0x50 / Write 0x50 + 百分比
 *   1  进度条
 *   2  容量：12.3/64.0KB + 百分比
 *   3  附加信息：型号/写入次数 或 提示语
 * ===================================================================== */

#include "board.h"

#define OLED_W              128
#define OLED_H              32
#define OLED_PAGES          4

#define OLED_PAGE_STATUS    0
#define OLED_PAGE_BAR       1
#define OLED_PAGE_INFO      2
#define OLED_PAGE_EXTRA     3

/* 1 = 已探测到屏并完成初始化（0x3C 或 0x3D）；0 = 没有可用屏 */
extern bit OLED_Found;

void    OLED_Init(void);

/* 写一行 8×8 文本（最多 16 字符，x 固定 0，右侧自动补空白） */
bit     OLED_Text(uint8_t page, const char *s);

/* 画进度条到 OLED_PAGE_BAR：外框 + 左对齐填充 */
bit     OLED_Bar(uint8_t percent);

/* ★ 整屏清空（四页全灭），并把差分缓存标脏：
 *   之后任何 OLED_Text / OLED_Bar 都会重发，不会被"内容没变"吃掉。
 *   用于切到"未插卡 / 未初始化"画面时彻底清掉上一屏的容量、型号、进度条。*/
void    OLED_Clear(void);

/* 探测到的 7 位地址（0x3C / 0x3D）；没有屏时返回 0 */
uint8_t OLED_Addr(void);

#endif /* __SSD13XX_H__ */
