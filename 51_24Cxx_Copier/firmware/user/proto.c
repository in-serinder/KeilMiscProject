/* =====================================================================
 * proto.c — 帧层实现
 *
 * 帧格式：'>' 内容 '<' CRLF
 *   - '>' 之前的一切字节（含孤立换行）都当噪声丢掉
 *   - 帧内出现 CR/LF -> 该帧判为非法，回 R-ERR
 *   - 帧内容超长 -> 丢弃并回 R-ERR
 *   - '<' 之后的 CRLF 属于帧间隔，自动忽略（因为已经在帧外）
 * ===================================================================== */
#include "proto.h"
#include "uart.h"
#include "delay.h"

static char  xdata s_rx[CFG_FRAME_MAX];
static uint8_t s_len;
static volatile bit s_in_frame;
static volatile bit s_ready;
static volatile bit s_bad;

void Proto_OnByte(uint8_t b)
{
    if (b == '>')
    {
        s_in_frame = 1;
        s_len = 0;
        s_bad = 0;
        return;
    }

    if (!s_in_frame)
    {
        return;                 /* 帧外的噪声（含 CR/LF）一律忽略 */
    }

    if (b == '<')
    {
        s_in_frame = 0;
        s_rx[s_len] = '\0';
        s_ready = 1;
        return;
    }

    if (b == '\r' || b == '\n')
    {
        s_in_frame = 0;         /* 帧内换行：非法帧 */
        s_bad = 1;
        s_len = 0;
        s_rx[0] = '\0';
        s_ready = 1;
        return;
    }

    if (s_len >= (CFG_FRAME_MAX - 1))
    {
        s_in_frame = 0;         /* 超长：丢弃 */
        s_bad = 1;
        s_len = 0;
        s_rx[0] = '\0';
        s_ready = 1;
        return;
    }

    s_rx[s_len++] = (char)b;
}

uint8_t Proto_PollFrame(char *buf)
{
    uint8_t i;

    if (!s_ready)
    {
        return PROTO_FRAME_NONE;
    }

    if (s_bad)
    {
        s_ready = 0;
        return PROTO_FRAME_BAD;
    }

    for (i = 0; i <= s_len; i++)        /* 连结尾 '\0' 一起拷 */
    {
        buf[i] = s_rx[i];
    }
    s_ready = 0;
    return PROTO_FRAME_OK;
}

uint8_t Proto_WaitFrame(char *buf, uint16_t timeout_ms)
{
    uint8_t r;

    for (;;)
    {
        r = Proto_PollFrame(buf);
        if (r != PROTO_FRAME_NONE)
        {
            return r;
        }
        if (timeout_ms == 0)
        {
            return PROTO_FRAME_NONE;
        }
        delay_ms(1);
        timeout_ms--;
    }
}

void Proto_SendAck(const char code *s)
{
    Uart1_SendByte('>');
    Uart1_SendStr(s);
    Uart1_SendByte('<');
    Uart1_SendByte('\r');
    Uart1_SendByte('\n');
}

void Proto_SendData(const char code *prefix, const uint8_t *buf, uint8_t n)
{
    uint8_t i;

    Uart1_SendByte('>');
    Uart1_SendStr(prefix);
    Uart1_SendByte('[');
    for (i = 0; i < n; i++)
    {
        if (i != 0)
        {
            Uart1_SendByte(',');
        }
        Uart1_SendHex(buf[i]);
    }
    Uart1_SendByte(']');
    Uart1_SendByte('<');
    Uart1_SendByte('\r');
    Uart1_SendByte('\n');
}
