/* =====================================================================
 * copier.c — 24Cxx 多路烧录器命令状态机
 *
 * 与模拟器 lower-level_simulation/eeprom_sim/state_machine.py 行为对齐：
 *
 *   上位机 ->                    MCU ->
 *   >ROUTER_0x50<                R-S         状态 ROUTER_SET
 *   >EDGE_0x1FFF<                R-S         状态 EDGE_SET
 *   >INT_R<                      R-S         状态 READ_WAIT，随后 MCU 主动推包
 *     >R_[16字节]<               RX-OK  ...
 *     >TX-OVER<                  读结束回 IDLE
 *   >INT_W<                      R-S         状态 WRITE_WAIT
 *     >W_[16字节]<               RX-OK -> 写 EEPROM -> R-S / R-ERR
 *   >TX-OVER<                    R-S         回 IDLE
 *   >ABORT<                      R-S         回 IDLE
 *   >TYPE_ECHO<                  YES         不影响状态
 *
 * 规则：
 *   有效长度 n = min(16, EDGE - 当前地址 + 1)
 *   写：只写 n 字节，成功才 addr += n；失败地址不动并回 IDLE
 *   读：读 n 字节，其余补 0x00 凑满 16 字节；每包等 RX-OK，2s 超时回 R-ERR
 *   任何错误统一回 R-ERR，不细分错误码
 * ===================================================================== */
#include "copier.h"
#include "proto.h"
#include "display.h"
#include "HW_24c64.h"
#include "delay.h"

static char    xdata s_cmd[CFG_FRAME_MAX];
static uint8_t xdata s_pkt[CFG_PKT_BYTES];

static uint8_t  s_state;      /* ST_xxx */
static uint8_t  s_router;     /* 7 位器件地址，0 = 未选 */
static uint32_t s_edge;       /* 末尾地址 */
static uint32_t s_addr;       /* 当前操作地址 */

/* ---------------- 小工具 ---------------- */
static bit eq_all(const char xdata *s, const char code *p)
{
    while (*p != '\0')
    {
        if (*s != *p)
        {
            return 0;
        }
        s++;
        p++;
    }
    return (bit)(*s == '\0');
}

static bit has_prefix(const char xdata *s, const char code *p)
{
    while (*p != '\0')
    {
        if (*s != *p)
        {
            return 0;
        }
        s++;
        p++;
    }
    return 1;
}

static bit hex_nibble(char c, uint8_t *v)
{
    if (c >= '0' && c <= '9') { *v = (uint8_t)(c - '0');      return 1; }
    if (c >= 'A' && c <= 'F') { *v = (uint8_t)(c - 'A' + 10); return 1; }
    if (c >= 'a' && c <= 'f') { *v = (uint8_t)(c - 'a' + 10); return 1; }
    return 0;
}

/* 解析 0xXXXX / XXXX（大小写不敏感，最多 5 位十六进制） */
static bit parse_addr(const char xdata *p, uint32_t *out)
{
    uint32_t v;
    uint8_t  n;
    uint8_t  d;

    v = 0;
    n = 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        p += 2;
    }

    while (*p != '\0')
    {
        if (n >= 5)
        {
            return 0;
        }
        if (!hex_nibble(*p, &d))
        {
            return 0;
        }
        v = (v << 4) | (uint32_t)d;
        n++;
        p++;
    }

    if (n == 0)
    {
        return 0;
    }

    *out = v;
    return 1;
}

/* 解析 W_ 后面的 "[d1,d2,...,d16]" */
static bit parse_pkt(const char xdata *p, uint8_t *out)
{
    uint8_t i;
    uint8_t h;
    uint8_t l;

    if (*p != '[')
    {
        return 0;
    }
    p++;

    for (i = 0; i < CFG_PKT_BYTES; i++)
    {
        if (!hex_nibble(p[0], &h))
        {
            return 0;
        }
        if (!hex_nibble(p[1], &l))
        {
            return 0;
        }
        out[i] = (uint8_t)((h << 4) | l);
        p += 2;

        if (i != (CFG_PKT_BYTES - 1))
        {
            if (*p != ',')
            {
                return 0;
            }
            p++;
        }
    }

    if (*p != ']')
    {
        return 0;
    }
    p++;

    return (bit)(*p == '\0');
}

/* ---------------- 状态辅助 ---------------- */
static void reply_ok(void)  { Proto_SendAck(ACK_RS); }
static void reply_err(void) { Proto_SendAck(ACK_ERR); }

static void to_idle(void)
{
    s_edge = 0;
    s_addr = 0;
    s_state = ST_IDLE;
    Disp_Update(s_state, s_router);     /* 保留已选器件地址，方便看板子状态 */
}

/* 本包有效字节数 = min(16, EDGE - 当前地址 + 1) */
static uint8_t pkt_len(void)
{
    uint32_t rem;

    rem = s_edge - s_addr + 1;
    if (rem > (uint32_t)CFG_PKT_BYTES)
    {
        return CFG_PKT_BYTES;
    }
    return (uint8_t)rem;
}

/* ---------------- 读流程（阻塞，直到 TX-OVER / 出错 / 中止） ---------------- */
static void run_read(void)
{
    uint8_t n;
    uint8_t i;
    uint8_t r;

    for (;;)
    {
        if (s_addr > s_edge)
        {
            reply_err();
            to_idle();
            return;
        }

        n = pkt_len();

        s_state = ST_READ_CURR;
        Disp_Update(s_state, s_router);

        if (!EE_ReadBlock(s_addr, s_pkt, n))
        {
            reply_err();
            to_idle();
            return;
        }
        for (i = n; i < CFG_PKT_BYTES; i++)
        {
            s_pkt[i] = 0x00;            /* 最后一包不足 16 字节补 0 */
        }

        Proto_SendData(DATA_R_PREFIX, s_pkt, CFG_PKT_BYTES);

        s_state = ST_READ_OVER;
        Disp_Update(s_state, s_router);

        /* 等上位机确认，最长 CFG_RXOK_TIMEOUT_MS */
        r = Proto_WaitFrame((char *)s_cmd, CFG_RXOK_TIMEOUT_MS);

        if ((r == PROTO_FRAME_OK) && eq_all((const char xdata *)s_cmd, ACK_RXOK))
        {
            s_addr += n;

            if (s_addr > s_edge)
            {
                Proto_SendAck(ACK_TXOVER);      /* 最后一包已确认 -> 结束标志 */
                s_state = ST_READ_WAIT;
                Disp_Update(s_state, s_router);
                to_idle();
                return;
            }

            s_state = ST_READ_WAIT;
            Disp_Update(s_state, s_router);
            continue;
        }

        if ((r == PROTO_FRAME_OK) && eq_all((const char xdata *)s_cmd, "ABORT"))
        {
            reply_ok();
            to_idle();
            return;
        }

        reply_err();                            /* 超时 / 非 RX-OK / 非法帧 */
        to_idle();
        return;
    }
}

/* ---------------- 各命令 ---------------- */
static void cmd_router(const char xdata *p)
{
    uint32_t v;

    if (!parse_addr(p, &v))
    {
        reply_err();
        return;
    }
    if (v < 0x50UL || v > 0x57UL)
    {
        reply_err();
        return;
    }
    if (!EE_Select((uint8_t)v))
    {
        reply_err();
        return;
    }

    s_router = (uint8_t)v;
    s_edge = 0;
    s_addr = 0;
    s_state = ST_ROUTER_SET;

    reply_ok();
    Disp_Update(s_state, s_router);
}

static void cmd_edge(const char xdata *p)
{
    uint32_t v;

    if (s_router == 0)
    {
        reply_err();                            /* 未选器件 */
        return;
    }
    if (!parse_addr(p, &v))
    {
        reply_err();
        return;
    }
    if (v > EE_MaxAddr())
    {
        reply_err();                            /* 越界 */
        return;
    }

    s_edge = v;
    s_addr = 0;
    s_state = ST_EDGE_SET;

    reply_ok();
    Disp_Update(s_state, s_router);
}

static void cmd_int_r(void)
{
    if (s_state != ST_EDGE_SET)
    {
        reply_err();
        return;
    }

    s_addr = 0;
    s_state = ST_READ_WAIT;

    reply_ok();
    Disp_Update(s_state, s_router);

    run_read();
}

static void cmd_int_w(void)
{
    if (s_state != ST_EDGE_SET)
    {
        reply_err();
        return;
    }

    s_addr = 0;
    s_state = ST_WRITE_WAIT;

    reply_ok();
    Disp_Update(s_state, s_router);
}

static void cmd_w(const char xdata *p)
{
    uint8_t n;

    if ((s_state != ST_WRITE_WAIT) && (s_state != ST_WRITE_OVER))
    {
        reply_err();
        return;
    }

    if (!parse_pkt(p, s_pkt))
    {
        reply_err();                            /* 格式/长度错 -> 结束写流程 */
        to_idle();
        return;
    }

    if (s_addr > s_edge)
    {
        reply_err();
        to_idle();
        return;
    }

    n = pkt_len();

    s_state = ST_WRITE_CURR;
    Disp_Update(s_state, s_router);

    Proto_SendAck(ACK_RXOK);                    /* 先确认收到整包 */

    if (!EE_WriteBlock(s_addr, s_pkt, n))
    {
        reply_err();                            /* 写失败：地址不增加 */
        to_idle();
        return;
    }

    s_addr += n;
    s_state = ST_WRITE_OVER;
    reply_ok();
    Disp_Update(s_state, s_router);
}

static void cmd_tx_over(void)
{
    if ((s_state != ST_WRITE_OVER) && (s_state != ST_WRITE_WAIT))
    {
        reply_err();
        return;
    }
    reply_ok();
    to_idle();
}

static void cmd_abort(void)
{
    to_idle();
    reply_ok();
}

/* ---------------- 命令分发 ---------------- */
static void dispatch(const char xdata *s)
{
    if (has_prefix(s, "ROUTER_"))
    {
        cmd_router(s + 7);
    }
    else if (has_prefix(s, "EDGE_"))
    {
        cmd_edge(s + 5);
    }
    else if (eq_all(s, "INT_R"))
    {
        cmd_int_r();
    }
    else if (eq_all(s, "INT_W"))
    {
        cmd_int_w();
    }
    else if (has_prefix(s, "W_"))
    {
        cmd_w(s + 2);
    }
    else if (eq_all(s, "TX-OVER"))
    {
        cmd_tx_over();
    }
    else if (eq_all(s, "RX-OK"))
    {
        reply_err();                            /* 顶层收到 RX-OK：协议不匹配 */
    }
    else if (eq_all(s, "ABORT"))
    {
        cmd_abort();
    }
    else if (eq_all(s, "TYPE_ECHO"))
    {
        Proto_SendAck(ACK_YES);                 /* 多设备总线上确认目标设备 */
    }
    else if (eq_all(s, "DISP_TEST"))
    {
        /* 调试命令（不在正式协议里）：先查 CLK/DIO 是否受控，再全段点亮 0.2s
         *   YES   = 引脚受控（此时肉眼应看到 8.8.8.8）
         *   R-ERR = 引脚不受控（P5.4 仍为 RST / 接线不对） */
        bit pins_ok;

        pins_ok = Disp_SelfTest();
        Disp_Update(s_state, s_router);

        if (pins_ok)
        {
            Proto_SendAck(ACK_YES);
        }
        else
        {
            reply_err();
        }
    }
    else if (eq_all(s, "I2C_TEST"))
    {
        /* 调试命令：只探测当前槽位的器件地址有没有 ACK，不碰数据
         *   YES   = 器件在线（接线、上拉、地址都对）
         *   R-ERR = NACK（没插片 / 地址不符 / 上拉缺失 / 引脚不对） */
        if (EE_Probe())
        {
            Proto_SendAck(ACK_YES);
        }
        else
        {
            reply_err();
        }
    }
    else if (eq_all(s, "SCAN"))
    {
        /* 调试命令：扫描 0x50~0x57，回 >SCAN_[XX]<
         *   XX = 位掩码，bit0=0x50 … bit7=0x57，1 表示该地址有器件应答
         *   例：只插了 0x50 一片 -> >SCAN_[01]<；0x50+0x57 -> >SCAN_[81]<
         * 用途：多路板先扫一遍，确认"到底哪个工位有片子、地址配错没有"。 */
        uint8_t mask;

        mask = EE_ScanMask();
        Proto_SendData("SCAN_", &mask, 1);
        Disp_Update(s_state, s_router);
    }
    else
    {
        reply_err();                            /* 未知/畸形帧 */
    }
}

/* ---------------- 对外 ---------------- */
void Copier_Init(void)
{
    s_state  = ST_IDLE;
    s_router = 0;
    s_edge   = 0;
    s_addr   = 0;

    Disp_Init();
}

void Copier_Task(void)
{
    uint8_t r;

    r = Proto_PollFrame((char *)s_cmd);
    if (r == PROTO_FRAME_NONE)
    {
        return;
    }
    if (r == PROTO_FRAME_BAD)
    {
        reply_err();                            /* 帧内换行 / 超长 */
        return;
    }

    dispatch((const char xdata *)s_cmd);
}
