#ifndef __APP_H__
#define __APP_H__

/* =====================================================================
 * app.h — 命令状态机（与 Capsa 上位机 / CapsaSim 协议 v1.0 对接）
 *
 * 支持的命令：
 *   TYPE-CAPASA                         -> [YES]
 *   SET_名称_WP_健康_容量_型号_修改_读取_介质_UID -> [SET-OK]/[SET_ERR]
 *   GET_!                               -> >INFO_...< / [SET_ERR]
 *   UG_n / UGS                          -> [UG-OK] / [UG-n] / [SET_ERR]
 *   DI_hash_count / DIG                 -> [DI-OK] / [DIG-hash-count]
 *   ROUTER_0xNN                         -> [RX-OK] / [RX-ERR]
 *   ADDR_0xNNNN                         -> [RX-OK] / [ERR_EXT]
 *   INT_W / W_... / TX-OVER             -> [RX-OK] / [ERR_WP] / [ERR_EXT]
 *   INT_R                               -> [RX-OK] 后主动推 >R_...<，等 [RX-OK]
 *   ABORT                               -> [ABORT-OK]
 * ===================================================================== */

#include "board.h"

#define APP_ST_IDLE     0
#define APP_ST_WRITE    1
#define APP_ST_READ     2

void App_Init(void);
void App_Handle(const char *cmd);
void App_Tick(void);            /* 空闲时刷新显示（目前只做节流）*/

#endif /* __APP_H__ */
