#ifndef __PROTO_H__
#define __PROTO_H__

/* =====================================================================
 * proto.h — 串口帧层（Capsa 协议 v1.0）
 *
 *   收：'>' 内容 '<'      帧外噪声（含 CR/LF）忽略，帧内 CR/LF 判非法
 *   发 ACK：[CODE] + CRLF
 *   发数据：>PAYLOAD< + CRLF
 *
 * 与 51_24Cxx_Copier 的差别：应答是方括号（[RX-OK]）而不是 R-S，
 * 与 CapsaSim / 上位机 CapsaProtocol 完全一致。
 * ===================================================================== */

#include "board.h"

#define PROTO_NONE      0
#define PROTO_OK        1
#define PROTO_BAD       2       /* 帧内换行 / 超长 */

void    Proto_OnByte(uint8_t b);            /* UART 中断里调用 */
uint8_t Proto_Poll(char *buf);              /* 取一帧，无帧立即返回 NONE */
uint8_t Proto_Wait(char *buf, uint16_t timeout_ms);

void    Proto_SendAck(const char *ack);                             /* [ack] */
void    Proto_SendData(const char *payload);                        /* >payload< */
void    Proto_SendR(const uint8_t *buf, uint8_t n);                 /* >R_AA,BB..< */

#endif /* __PROTO_H__ */
