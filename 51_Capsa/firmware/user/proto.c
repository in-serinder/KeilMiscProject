#include "board.h"
/* proto.c — 帧层实现（'>' 组帧，'[' 应答）*/
#include "proto.h"
#include "uart.h"
#include "delay.h"

static char xdata s_rx[CFG_FRAME_MAX];
static uint8_t s_len;
static uint8_t s_close;             /* 当前帧的结束符：'<' 或 ']' */
static volatile bit s_in;
static volatile bit s_ready;
static volatile bit s_bad;

void Proto_OnByte(uint8_t b)
{
    /* 两种帧都收：
     *   >命令<   上位机发的命令 / 模拟器
     *   [应答]   上位机在读会话里回给下位机的 [RX-OK]（方括号）
     * 早期只认尖括号，导致读会话里上位机每包回的 [RX-OK] 全被当成噪声丢掉，
     * 下位机白等 2 秒超时后退出 —— 现象是"只读到第一包"，芯片测试因此 0/8。*/
    if (b == '>' || b == '[')
    {
        s_close = (b == '[') ? ']' : '<';
        s_in = 1;
        s_len = 0;
        s_bad = 0;
        return;
    }

    if (!s_in)
    {
        return;                     /* 帧外噪声（CR/LF、空格）丢弃 */
    }

    if (b == s_close)
    {
        s_in = 0;
        s_rx[s_len] = '\0';
        s_ready = 1;
        return;
    }

    /* 括号不配对：判坏帧，别把内容当命令执行 */
    if (b == '<' || b == ']')
    {
        s_in = 0;
        s_bad = 1;
        s_len = 0;
        s_rx[0] = '\0';
        s_ready = 1;
        return;
    }

    if (b == '\r' || b == '\n')
    {
        s_in = 0;
        s_bad = 1;
        s_len = 0;
        s_rx[0] = '\0';
        s_ready = 1;
        return;
    }

    if (s_len >= (CFG_FRAME_MAX - 1))
    {
        s_in = 0;
        s_bad = 1;
        s_len = 0;
        s_rx[0] = '\0';
        s_ready = 1;
        return;
    }

    s_rx[s_len++] = (char)b;
}

uint8_t Proto_Poll(char *buf)
{
    uint8_t i;
    uint8_t len;
    uint8_t r;

    ES = 0;                         /* 进入临界区：拷贝期间不接新帧 */
    if (!s_ready)
    {
        ES = 1;
        return PROTO_NONE;
    }

    if (s_bad)
    {
        s_ready = 0;
        ES = 1;
        return PROTO_BAD;
    }

    len = s_len;
    for (i = 0; i <= len; i++)
    {
        buf[i] = s_rx[i];
    }
    s_ready = 0;
    r = PROTO_OK;
    ES = 1;
    return r;
}

uint8_t Proto_Wait(char *buf, uint16_t timeout_ms)
{
    uint8_t r;

    for (;;)
    {
        r = Proto_Poll(buf);
        if (r != PROTO_NONE)
        {
            return r;
        }
        if (timeout_ms == 0)
        {
            return PROTO_NONE;
        }
        delay_ms(1);
        timeout_ms--;
    }
}

void Proto_SendAck(const char *ack)
{
    Uart1_SendByte('[');
    Uart1_SendStr(ack);
    Uart1_SendByte(']');
    Uart1_SendByte('\r');
    Uart1_SendByte('\n');
}

void Proto_SendData(const char *payload)
{
    Uart1_SendByte('>');
    Uart1_SendStr(payload);
    Uart1_SendByte('<');
    Uart1_SendByte('\r');
    Uart1_SendByte('\n');
}

void Proto_SendR(const uint8_t *buf, uint8_t n)
{
    uint8_t i;

    Uart1_SendByte('>');
    Uart1_SendStr("R_");
    for (i = 0; i < n; i++)
    {
        if (i != 0)
        {
            Uart1_SendByte(',');
        }
        Uart1_SendHex(buf[i]);
    }
    Uart1_SendByte('<');
    Uart1_SendByte('\r');
    Uart1_SendByte('\n');
}
