#ifndef __COPIER_H__
#define __COPIER_H__

/* =====================================================================
 * copier.h — 命令状态机（协议见 lower-level_simulation/CMD.md）
 *
 * 状态编号必须与 display.c 的前缀表、模拟器 state_machine.py 一一对应。
 * ===================================================================== */

#include "board.h"

#define ST_IDLE         0   /* 空闲 */
#define ST_ROUTER_SET   1   /* 已选器件，等 EDGE */
#define ST_EDGE_SET     2   /* 已设 EDGE，等 INT_R / INT_W */
#define ST_WRITE_WAIT   3   /* 写等待：等 W_ 包 */
#define ST_WRITE_CURR   4   /* 正在写当前包 */
#define ST_WRITE_OVER   5   /* 当前包写完成 */
#define ST_READ_WAIT    6   /* 读等待：已确认上一包 */
#define ST_READ_CURR    7   /* 正在读当前包 */
#define ST_READ_OVER    8   /* 当前包读完成，等 RX-OK */

void Copier_Init(void);
void Copier_Task(void);     /* 主循环里反复调用 */

#endif /* __COPIER_H__ */
