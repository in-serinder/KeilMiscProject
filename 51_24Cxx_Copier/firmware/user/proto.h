#ifndef __PROTO_H__
#define __PROTO_H__

/* =====================================================================
 * proto.h — 串口帧层：>内容< + CRLF
 *
 *   接收：UART 中断里逐字节组帧，遇到 '<' 置"帧就绪"标志
 *   发送：Proto_SendAck / Proto_SendData
 * ===================================================================== */

#include "board.h"

/* 应答帧内容（不含 > <） */
#define ACK_RS          "R-S"
#define ACK_ERR         "R-ERR"
#define ACK_RXOK        "RX-OK"
#define ACK_TXOVER      "TX-OVER"
#define ACK_YES         "YES"
#define DATA_R_PREFIX   "R_"

/* Proto_PollFrame / Proto_WaitFrame 的返回值 */
#define PROTO_FRAME_NONE    0
#define PROTO_FRAME_OK      1
#define PROTO_FRAME_BAD     2   /* 帧内换行 / 超长 / 溢出 */

void    Proto_OnByte(uint8_t b);        /* 由 UART 中断调用 */
uint8_t Proto_PollFrame(char *buf);     /* 取一帧，无帧立即返回 NONE */
uint8_t Proto_WaitFrame(char *buf, uint16_t timeout_ms);

void    Proto_SendAck(const char code *s);
void    Proto_SendData(const char code *prefix, const uint8_t *buf, uint8_t n);

#endif /* __PROTO_H__ */
