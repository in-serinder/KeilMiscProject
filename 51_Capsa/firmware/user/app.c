#include "board.h"
/* app.c — 命令状态机（Capsa 协议 v1.0）*/
#include "app.h"
#include "proto.h"
#include "ee.h"
#include "meta.h"
#include "pcf8574.h"
#include "ui.h"
#include "i2c_sw.h"
#include "i2c_hw.h"
#include "ssd13xx.h"
#include "delay.h"

/* 存储卡三态（App_Tick 周期检测 + card_state_set 翻转显示）：
 *   OFFLINE = 24C02 无应答（未插卡）；UNINIT = 可通讯但 init_flag=0；
 *   READY = 可通讯且有属性数据。*/
#define CARD_OFFLINE            0
#define CARD_UNINIT             1
#define CARD_READY              2

static void card_state_set(uint8_t st);         /* 翻转卡状态并刷新 OLED */

#define ROUTER_NONE     0xFF

static uint8_t xdata s_state = APP_ST_IDLE;
static uint8_t xdata s_router = ROUTER_NONE;    /* 0..7 -> 0x50..0x57 */
static uint32_t xdata s_edge = 0;               /* 片内末尾地址 */
static uint32_t xdata s_addr = 0;               /* 当前指针 */
static bit     s_has_edge = 0;

static char xdata s_cmd[CFG_FRAME_MAX];
static uint8_t xdata s_pkt[CFG_PKT_BYTES];
static char xdata s_out[112];               /* INFO / 数值拼装缓冲 */
static uint8_t xdata s_last_pct = 0xFF;
static const char *xdata s_f[9];      /* SET 字段指针表（九项）*/
static uint8_t xdata s_hash[8];       /* DI 哈希暂存 */
static uint8_t xdata s_num[10];       /* 十进制转换缓冲 */

/* ---------------------------------------------------------------------
 * 字符串 / 数字工具（C51 里避免依赖 printf）
 * ------------------------------------------------------------------- */
static bit str_eq(const char *s, const char *p)
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

static bit str_prefix(const char *s, const char *p)
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

static uint8_t str_len(const char *s)
{
    uint8_t n = 0;

    while (*s != 0)
    {
        n++;
        s++;
    }
    return n;
}

static bit hex_nib(char c, uint8_t *v)
{
    if (c >= '0' && c <= '9') { *v = (uint8_t)(c - '0');      return 1; }
    if (c >= 'A' && c <= 'F') { *v = (uint8_t)(c - 'A' + 10); return 1; }
    if (c >= 'a' && c <= 'f') { *v = (uint8_t)(c - 'a' + 10); return 1; }
    return 0;
}

/* 十进制或 0x 前缀十六进制 */
static bit parse_num(const char *p, uint32_t *out)
{
    uint32_t v = 0;
    uint8_t n = 0;
    uint8_t d;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        p += 2;
        while (*p != '\0')
        {
            if (n >= 8 || !hex_nib(*p, &d))
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

    while (*p != '\0')
    {
        if (*p < '0' || *p > '9')
        {
            return 0;
        }
        if (n >= 10)                /* 32 位最多 10 位十进制（epoch 时间戳就是 10 位）*/
        {
            return 0;
        }
        v = v * 10UL + (uint32_t)(*p - '0');
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

static uint8_t put_dec(char *p, uint32_t v)
{
    uint8_t n = 0;
    uint8_t i;

    if (v == 0)
    {
        p[0] = '0';
        return 1;
    }
    while (v > 0)
    {
        s_num[n++] = (uint8_t)(v % 10UL);
        v = v / 10UL;
    }
    for (i = 0; i < n; i++)
    {
        p[i] = (char)('0' + s_num[n - 1 - i]);
    }
    return n;
}

static uint8_t put_hex(char *p, uint8_t b)
{
    const char code HEX[] = "0123456789ABCDEF";

    p[0] = HEX[(b >> 4) & 0x0F];
    p[1] = HEX[b & 0x0F];
    return 2;
}

/* ---------------------------------------------------------------------
 * 显示
 * ------------------------------------------------------------------- */
static void show_task(bit is_read)
{
    uint8_t pct = 0;
    uint32_t total;
    uint32_t done;

    if (s_router == ROUTER_NONE || !s_has_edge)
    {
        return;
    }

    total = (uint32_t)CFG_CHIP_COUNT * (s_edge + 1UL);
    done = (uint32_t)s_router * (s_edge + 1UL) + s_addr;

    if (total > 0)
    {
        pct = (uint8_t)((done * 100UL) / total);
    }
    if (pct > 100)
    {
        pct = 100;
    }

    /* 2% 步进节流，与 ui.c 里的进度节流保持一致。
     * 驱动层已按页做差分：状态行/进度条各自只在内容变化时才发一次事务，
     * 所以这里放宽到 50 级也不会明显拖慢写周期（单步约 6ms）。*/
    if (pct != 100 && s_last_pct != 0xFF && (uint8_t)(pct / 2U) == (uint8_t)(s_last_pct / 2U))
    {
        return;
    }
    s_last_pct = pct;

    UI_Task(is_read, (uint8_t)(ADDR_ARRAY_BASE + s_router), pct);
}

/* ---------------------------------------------------------------------
 * 写保护检查
 * ------------------------------------------------------------------- */
static bit wp_blocked(void)
{
    if (s_router == ROUTER_NONE)
    {
        return 0;
    }
    return (bit)((g_meta.wp_mask & (uint8_t)(1U << s_router)) ? 1 : 0);
}

/* ---------------------------------------------------------------------
 * SET / GET
 * ------------------------------------------------------------------- */
static bit parse_mask_field(const char *p, uint8_t *out)
{
    uint8_t hi;
    uint8_t lo;
    uint8_t v;
    uint8_t i;

    if (p[0] == '!')
    {
        return 0;                   /* 表示不修改 */
    }

    if (p[0] != '\0' && p[1] != '\0' && p[2] == '\0')
    {
        if (hex_nib(p[0], &hi) && hex_nib(p[1], &lo))
        {
            *out = (uint8_t)((hi << 4) | lo);
            return 1;
        }
    }

    /* 兼容旧的 8 位二进制写法 00000011 */
    v = 0;
    for (i = 0; i < 8; i++)
    {
        if (p[i] == '\0')
        {
            return 0;
        }
        if (p[i] != '0' && p[i] != '1')
        {
            return 0;
        }
        v = (uint8_t)((v << 1) | (uint8_t)(p[i] - '0'));
    }
    if (p[8] != '\0')
    {
        return 0;
    }
    *out = v;
    return 1;
}

static bit parse_uid(const char *p, uint8_t *uid)
{
    uint8_t i;
    uint8_t hi;
    uint8_t lo;

    for (i = 0; i < 6; i++)
    {
        if (!hex_nib(p[i * 2], &hi) || !hex_nib(p[i * 2 + 1], &lo))
        {
            return 0;
        }
        uid[i] = (uint8_t)((hi << 4) | lo);
    }
    return (bit)(p[12] == '\0');
}

static void handle_set(const char *body)
{
    /* body 指向 SET_ 之后 */
    uint8_t nf = 0;
    const char *p = body;
    bit first;
    uint8_t tmp8;
    uint32_t tmp32;
    uint8_t i;

    /* 切分 9 个字段（名称里不允许出现 '_'，协议已保证）*/
    s_f[nf++] = p;
    while (*p != '\0')
    {
        if (*p == '_')
        {
            if (nf >= 9)
            {
                Proto_SendAck("SET_ERR");
                return;
            }
            *((char *)p) = '\0';    /* 就地切断：s_cmd 是可写缓冲 */
            s_f[nf++] = p + 1;
        }
        p++;
    }

    if (nf != 9)
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    first = (bit)(Meta_IsInitialized() ? 0 : 1);

    /* 1) 名称 */
    if (s_f[0][0] != '!')
    {
        uint8_t k = 0;

        if (str_len(s_f[0]) > META_NAME_LEN)
        {
            Proto_SendAck("SET_ERR");
            return;
        }
        for (i = 0; i < META_NAME_LEN; i++)
        {
            g_meta.name[i] = (i < str_len(s_f[0])) ? s_f[0][i] : '\0';
        }
        g_meta.name[META_NAME_LEN] = '\0';
    }
    else if (first)
    {
        Proto_SendAck("SET_ERR");   /* 首次初始化必须给名称 */
        return;
    }

    /* 2) WP 掩码 */
    if (parse_mask_field(s_f[1], &tmp8))
    {
        g_meta.wp_mask = tmp8;
    }
    else if (s_f[1][0] != '!')
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    /* 3) 健康掩码 */
    if (parse_mask_field(s_f[2], &tmp8))
    {
        g_meta.health_mask = tmp8;
    }
    else if (s_f[2][0] != '!')
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    /* 4) 阵列容量 KB */
    if (s_f[3][0] != '!')
    {
        if (!parse_num(s_f[3], &tmp32) || tmp32 > 0xFFFFUL)
        {
            Proto_SendAck("SET_ERR");
            return;
        }
        g_meta.cap_kb = (uint16_t)tmp32;
    }

    /* 5) 型号 Kbit */
    if (s_f[4][0] != '!')
    {
        if (!parse_num(s_f[4], &tmp32))
        {
            Proto_SendAck("SET_ERR");
            return;
        }
        if (tmp32 == 0)
        {
            tmp32 = EE_MODEL_24C64;         /* 0 按 24C64 处理 */
        }
        if (tmp32 != EE_MODEL_24C32 && tmp32 != EE_MODEL_24C64 &&
            tmp32 != EE_MODEL_24C128 && tmp32 != EE_MODEL_24C256 &&
            tmp32 != EE_MODEL_24C512 && tmp32 != EE_MODEL_24C1024)
        {
            Proto_SendAck("SET_ERR");
            return;
        }
        g_meta.model_kbit = tmp32;
    }
    else if (first)
    {
        g_meta.model_kbit = EE_MODEL_24C64;
    }

    /* 6) 修改时间 */
    if (s_f[5][0] != '!')
    {
        if (!parse_num(s_f[5], &g_meta.mtime))
        {
            Proto_SendAck("SET_ERR");
            return;
        }
    }

    /* 7) 读取时间 */
    if (s_f[6][0] != '!')
    {
        if (!parse_num(s_f[6], &g_meta.rtime))
        {
            Proto_SendAck("SET_ERR");
            return;
        }
    }

    /* 8) 介质 */
    if (s_f[7][0] != '!')
    {
        if (!parse_num(s_f[7], &tmp32) || tmp32 > 3)
        {
            Proto_SendAck("SET_ERR");
            return;
        }
        g_meta.medium = (uint8_t)tmp32;
    }

    /* 9) UID */
    if (s_f[8][0] != '!')
    {
        if (!parse_uid(s_f[8], g_meta.uid))
        {
            Proto_SendAck("SET_ERR");
            return;
        }
    }
    else if (first)
    {
        for (i = 0; i < META_UID_LEN; i++)
        {
            g_meta.uid[i] = (uint8_t)(i + 1);   /* 兜底 UID，正常上位机都会给 */
        }
    }

    g_meta.init_flag = 1;
    EE_SetModel((uint16_t)g_meta.model_kbit);

    /* ★ 容量一律按"可写片数 × 单片容量"重算（覆盖 SET 第 4 字段传来的值）：
     *   上位机点击 WP 徽章只发 SET_!_<mask>_!…_!，其余字段占位，
     *   容量字段不会跟着变 —— 只有固件重算这一条路能把 cap_kb 从
     *   "8 片口径"修正为"可写片口径"，并随下面的 SaveInfo/FirstSet 落盘。 */
    Meta_RecalcCapacity();

    if (first)
    {
        if (!Meta_FirstSet())
        {
            Proto_SendAck("SET_ERR");
            return;
        }
    }
    else
    {
        if (!Meta_SaveInfo())
        {
            Proto_SendAck("SET_ERR");
            return;
        }
    }

    /* WP 掩码变化 -> 立即下发 PCF8574 */
    (void)PCF_SetMask(g_meta.wp_mask);

    Proto_SendAck("SET-OK");

    /* 卡从此有了属性数据：先开总闸，再切状态（状态可能已是 READY 而早退），
     * 最后强制重绘型号行/容量行 —— 不然差分缓存里还是"未初始化"那几行。*/
    UI_SetCardReady(1);
    card_state_set(CARD_READY);
    UI_RefreshMeta();
}

static void handle_get(void)
{
    uint8_t n = 0;
    uint8_t i;

    if (!Meta_IsInitialized())
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    s_out[n++] = 'I'; s_out[n++] = 'N'; s_out[n++] = 'F'; s_out[n++] = 'O';
    s_out[n++] = '_';

    /* 名称 */
    for (i = 0; i < META_NAME_LEN && g_meta.name[i] != '\0'; i++)
    {
        s_out[n++] = g_meta.name[i];
    }
    if (i == 0)
    {
        s_out[n++] = 'C'; s_out[n++] = 'A'; s_out[n++] = 'P'; s_out[n++] = 'S';
        s_out[n++] = 'A';
    }
    s_out[n++] = '_';

    /* WP / 健康掩码（2 位十六进制）*/
    n = (uint8_t)(n + put_hex(&s_out[n], g_meta.wp_mask));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], g_meta.health_mask));
    s_out[n++] = '_';

    /* 容量 KB */
    n = (uint8_t)(n + put_dec(&s_out[n], g_meta.cap_kb));
    s_out[n++] = '_';

    /* 型号 Kbit（以驱动里的当前型号为准）*/
    n = (uint8_t)(n + put_dec(&s_out[n], EE_GetModel()));
    s_out[n++] = '_';

    /* 修改时间 / 读取时间 */
    n = (uint8_t)(n + put_dec(&s_out[n], g_meta.mtime));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_dec(&s_out[n], g_meta.rtime));
    s_out[n++] = '_';

    /* 介质 */
    n = (uint8_t)(n + put_dec(&s_out[n], g_meta.medium));
    s_out[n++] = '_';

    /* UID（12 位十六进制）*/
    for (i = 0; i < META_UID_LEN; i++)
    {
        n = (uint8_t)(n + put_hex(&s_out[n], g_meta.uid[i]));
    }

    s_out[n] = '\0';
    Proto_SendData(s_out);
}

/* ---------------------------------------------------------------------
 * UG / UGS
 * ------------------------------------------------------------------- */
static void handle_ug(const char *p)
{
    uint32_t v;
    uint32_t cap;

    if (!Meta_IsInitialized())
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    if (!parse_num(p, &v))
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    /* 上限 = 有效容量（可写片 × 单片）。WP 片写不进去，used 不可能越过它们 */
    cap = Meta_ArrayBytes();
    if (v > cap)
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    Meta_SetUsed(v);
    if (!Meta_SaveUsed())
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    Proto_SendAck("UG-OK");
    UI_Capacity(g_meta.used, cap);
}

static void handle_ugs(void)
{
    uint8_t n;

    if (!Meta_IsInitialized())
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    s_out[0] = 'U';
    s_out[1] = 'G';
    s_out[2] = '-';
    n = (uint8_t)(3 + put_dec(&s_out[3], g_meta.used));
    s_out[n] = '\0';
    Proto_SendAck(s_out);
}

/* ---------------------------------------------------------------------
 * DI / DIG
 * ------------------------------------------------------------------- */
static void handle_di(const char *p)
{
    /* 格式：hash16_count，任一可为 '!' */
    bit has_hash = 0;
    bit has_count = 0;
    uint32_t count = 0;
    uint8_t i;
    uint8_t hi;
    uint8_t lo;
    const char *q;

    if (!Meta_IsInitialized())
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    q = p;
    if (q[0] != '!')
    {
        for (i = 0; i < 8; i++)
        {
            if (!hex_nib(q[i * 2], &hi) || !hex_nib(q[i * 2 + 1], &lo))
            {
                Proto_SendAck("RX-ERR");
                return;
            }
            s_hash[i] = (uint8_t)((hi << 4) | lo);
        }
        if (q[16] != '_')
        {
            Proto_SendAck("RX-ERR");
            return;
        }
        has_hash = 1;
        q += 17;
    }
    else
    {
        if (q[1] != '_')
        {
            Proto_SendAck("RX-ERR");
            return;
        }
        q += 2;
    }

    if (q[0] != '!')
    {
        if (!parse_num(q, &count))
        {
            Proto_SendAck("RX-ERR");
            return;
        }
        has_count = 1;
    }

    Meta_SetDataInfo(has_hash, s_hash, has_count, count);
    if (!Meta_SaveDataInfo())
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    Proto_SendAck("DI-OK");
}

/* SCAN：把 P_SW2.I2C_S 的 4 种取值挨个扫一遍，看阵列到底挂在哪组引脚上。
 * 应答 [SCAN_<m0><o0>_<m1><o1>_<m2><o2>_<m3><o3>]
 *   mN = 0x50~0x57 的应答位图（2 位十六进制）
 *   oN = 该组上 OLED(0x3C/0x3D) 是否应答（1/0）
 * 判读：哪一组 mN 接近 FF，阵列就在那组；若某组 oN=1，说明那组其实接到了
 *       IIC2 的器件（即硬件 I2C 与软件 I2C 落在同一组引脚上）。*/
static void handle_scan(void)
{
    uint8_t n = 0;
    uint8_t i;
    uint8_t m;

    s_out[n++] = 'S'; s_out[n++] = 'C'; s_out[n++] = 'A'; s_out[n++] = 'N';
    s_out[n++] = '_';

    for (i = 0; i < 4; i++)
    {
        I2C1_SetPinSel(i);

        if (i != 0)
        {
            s_out[n++] = '_';
        }

        m = EE_ScanMask();
        n = (uint8_t)(n + put_hex(&s_out[n], m));

        if (I2C1_Probe(ADDR_OLED) || I2C1_Probe((uint8_t)(ADDR_OLED + 1)))
        {
            s_out[n++] = '1';
        }
        else
        {
            s_out[n++] = '0';
        }
    }

    I2C1_SetPinSel(CFG_I2C_S_SEL & 0x03);   /* 恢复默认引脚组 */
    I2C2_Recover();                         /* 若刚才动到 IIC2 的脚，重新同步总线 */

    s_out[n] = 0;
    Proto_SendAck(s_out);
}

/* PINX：把 P1.4/P1.5/P5.4/P5.5 逐个推挽拉低并读回，同时回读模式寄存器本值。
 * 应答 [PINX_<可驱动位图>_<P1M0>_<P1M1>_<P5M0>_<P5M1>_<P1>_<P5>_<P_SW2>]
 *   位图 bit0=P1.4 bit1=P1.5 bit2=P5.4 bit3=P5.5，1 = 能拉低
 * 用途：区分"引脚没接"和"模式寄存器写不进去"——若位图全 0 但 M0/M1 已经是
 *       推挽值，说明 SFR 写生效了、引脚本身不可控（未bonding / 被别的功能占用）。*/
static void handle_pinx(void)
{
    uint8_t m = 0;
    uint8_t n = 0;
    uint8_t p1m0;
    uint8_t p1m1;
    uint8_t p5m0;
    uint8_t p5m1;
    uint8_t p1v;
    uint8_t p5v;

    P1M1 &= (uint8_t)~0x30;
    P1M0 |= 0x30;
    P1 &= (uint8_t)~0x30;
    delay_us(2);

    p1m0 = P1M0;
    p1m1 = P1M1;
    p1v = P1;
    if ((p1v & 0x10) == 0)
    {
        m |= 0x01;
    }
    if ((p1v & 0x20) == 0)
    {
        m |= 0x02;
    }

    P1 |= 0x30;
    P1M1 = 0xFF;                    /* 恢复高阻输入 */
    P1M0 = 0x00;

    P5M1 &= (uint8_t)~0x30;
    P5M0 |= 0x30;
    P5 &= (uint8_t)~0x30;
    delay_us(2);

    p5m0 = P5M0;
    p5m1 = P5M1;
    p5v = P5;
    if ((p5v & 0x10) == 0)
    {
        m |= 0x04;
    }
    if ((p5v & 0x20) == 0)
    {
        m |= 0x08;
    }

    P5 |= 0x30;
    P5M1 &= (uint8_t)~0x30;
    P5M0 &= (uint8_t)~0x30;

    I2C2_RestorePins();             /* 别把当前引脚组留在高阻，否则后面 I2C2 全废 */

    s_out[n++] = 'P'; s_out[n++] = 'I'; s_out[n++] = 'N'; s_out[n++] = 'X';
    s_out[n++] = '_';
    s_out[n++] = (char)('0' + (char)I2C2_PinPair());   /* 0 = P5 组，1 = P1 组 */
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], m));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], p1m0));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], p1m1));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], p5m0));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], p5m1));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], p1v));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], p5v));
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], P_SW2));

    s_out[n] = 0;
    Proto_SendAck(s_out);
}

/* DIAG：分线用。应答 [DIAG_<P5引脚>_<OLED地址>_<PCF>_<24C02>_<阵列位图>]
 *   P5  : 1 = P5.4/P5.5 能推挽拉低并读回（真在 CPU 手上）
 *         0 = 拉不低 → P5.4 多半还是复位脚（ISP 没勾"复位脚用作 I/O"）
 *   OLED : 3C / 3D，00 = 没探到
 *   PCF / 24C02 : 1 = 该地址有 ACK
 *   阵列位图 : bit0 = 0x50 ... bit7 = 0x57 */
static void handle_diag(void)
{
    uint8_t n = 0;

    s_out[n++] = 'D'; s_out[n++] = 'I'; s_out[n++] = 'A'; s_out[n++] = 'G';
    s_out[n++] = '_';

    s_out[n++] = (char)(Board_SwI2cPinsOk() ? '1' : '0');
    s_out[n++] = '_';

    n = (uint8_t)(n + put_hex(&s_out[n], OLED_Addr()));
    s_out[n++] = '_';
    s_out[n++] = (char)(I2C2_Probe(ADDR_PCF8574) ? '1' : '0');
    s_out[n++] = '_';
    s_out[n++] = (char)(I2C2_Probe(ADDR_META_24C02) ? '1' : '0');
    s_out[n++] = '_';
    n = (uint8_t)(n + put_hex(&s_out[n], EE_ScanMask()));
    s_out[n++] = '_';
    /* IIC1 空闲电平：bit0=SCL bit1=SDA，正常应是 3。
     * 若 < 3，说明那根线被拉死/没上拉 —— 此时上面的阵列位图不可信。*/
    s_out[n++] = (char)('0' + (char)(I2C1_BusLevels() & 0x03));

    s_out[n] = '\0';
    Proto_SendAck(s_out);
}

/* PINS：让 IIC2 的 SCL/SDA 以 2Hz 慢闪 3 秒，用于肉眼确认引脚是否受控。
 * 应答 [PINS-OK] / [PINS-NG]（NG = 推挽拉不低，P5.4 多半还是复位脚）。*/
static void handle_pins(void)
{
    uint8_t i;
    bit ok = Board_SwI2cPinsOk();

    for (i = 0; i < 6; i++)
    {
        SW_SCL = 0;
        SW_SDA = 0;
        delay_ms(250);
        SW_SCL = 1;
        SW_SDA = 1;
        delay_ms(250);
    }

    I2C2_Init();                    /* 闪完把总线恢复到空闲高电平 */

    Proto_SendAck(ok ? "PINS-OK" : "PINS-NG");
}

static void handle_dig(void)
{
    uint8_t n = 0;
    uint8_t i;

    if (!Meta_IsInitialized())
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    s_out[n++] = 'D'; s_out[n++] = 'I'; s_out[n++] = 'G'; s_out[n++] = '-';

    for (i = 0; i < META_HASH_LEN; i++)
    {
        n = (uint8_t)(n + put_hex(&s_out[n], g_meta.hash[i]));
    }

    s_out[n++] = '-';
    n = (uint8_t)(n + put_dec(&s_out[n], g_meta.write_count));
    s_out[n] = '\0';

    Proto_SendAck(s_out);
}

/* ---------------------------------------------------------------------
 * ROUTER / ADDR / 读写会话
 * ------------------------------------------------------------------- */
static void handle_router(const char *p)
{
    uint32_t v;

    if (!parse_num(p, &v))
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    /* 接受 0x50~0x57，也接受索引 0~7 */
    if (v >= ADDR_ARRAY_BASE && v <= (uint32_t)(ADDR_ARRAY_BASE + 7))
    {
        s_router = (uint8_t)(v - ADDR_ARRAY_BASE);
    }
    else if (v <= 7)
    {
        s_router = (uint8_t)v;
    }
    else
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    s_state = APP_ST_IDLE;
    s_has_edge = 0;
    s_edge = 0;
    s_addr = 0;

    Proto_SendAck("RX-OK");
}

static void handle_addr(const char *p)
{
    uint32_t v;

    if (s_router == ROUTER_NONE)
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    if (!parse_num(p, &v))
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    if (v > EE_MaxAddr())
    {
        Proto_SendAck("ERR_EXT");
        return;
    }

    s_edge = v;
    s_has_edge = 1;
    s_state = APP_ST_IDLE;
    s_addr = 0;

    Proto_SendAck("RX-OK");
}

static void handle_int_w(void)
{
    /* ★ 未初始化（属性不自洽）的卡不许开写入会话：
     *   否则 UI_Task 会把 "Write 0x50 45%" 画到"未初始化"画面上，
     *   而且会往一张没有属性记录的卡里写数据。上位机必须先 SET 初始化。*/
    if (!Meta_InfoValid())
    {
        Proto_SendAck("SET_ERR");
        return;
    }

    if (s_router == ROUTER_NONE || !s_has_edge)
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    if (wp_blocked())
    {
        UI_Message("Write Protected");
        Proto_SendAck("ERR_WP");
        return;
    }

    s_state = APP_ST_WRITE;
    s_addr = 0;
    s_last_pct = 0xFF;
    UI_Task(0, (uint8_t)(ADDR_ARRAY_BASE + s_router), 0);

    Proto_SendAck("RX-OK");
}

static bit parse_w_packet(const char *p, uint8_t *out, uint8_t *count)
{
    uint8_t n = 0;
    uint8_t hi;
    uint8_t lo;

    while (*p != '\0')
    {
        if (n >= CFG_PKT_BYTES)
        {
            return 0;
        }
        if (!hex_nib(p[0], &hi) || !hex_nib(p[1], &lo))
        {
            return 0;
        }
        out[n++] = (uint8_t)((hi << 4) | lo);
        p += 2;

        if (*p == ',')
        {
            p++;
        }
        else if (*p != '\0')
        {
            return 0;
        }
    }

    if (n == 0)
    {
        return 0;
    }
    *count = n;
    return 1;
}

static void handle_w(const char *p)
{
    uint8_t n;
    uint32_t room;

    if (s_state != APP_ST_WRITE)
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    if (wp_blocked())
    {
        Proto_SendAck("ERR_WP");
        return;
    }

    if (s_addr > s_edge)
    {
        Proto_SendAck("ERR_EXT");
        return;
    }

    if (!parse_w_packet(p, s_pkt, &n))
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    room = s_edge - s_addr + 1UL;
    if (room == 0)
    {
        Proto_SendAck("ERR_EXT");
        return;
    }

    if (n > room)
    {
        n = (uint8_t)room;              /* 末包不越过 ADDR */
    }

    if (!EE_WriteBlock(s_router, s_addr, s_pkt, n))
    {
        Proto_SendAck("RX-ERR");
        s_state = APP_ST_IDLE;
        return;
    }

    s_addr += n;
    Proto_SendAck("RX-OK");

    /* 进度显示（5% 节流在 show_task 里）*/
    show_task(0);
}

static void handle_int_r(void)
{
    if (s_router == ROUTER_NONE || !s_has_edge)
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    s_state = APP_ST_READ;
    s_addr = 0;
    s_last_pct = 0xFF;
    UI_Task(1, (uint8_t)(ADDR_ARRAY_BASE + s_router), 0);

    Proto_SendAck("RX-OK");

    /* 主动推包，每包等上位机 [RX-OK] */
    while (s_addr <= s_edge)
    {
        uint32_t room = s_edge - s_addr + 1UL;
        uint8_t n = (uint8_t)((room > CFG_PKT_BYTES) ? CFG_PKT_BYTES : room);
        uint8_t i;
        uint8_t r;

        for (i = 0; i < CFG_PKT_BYTES; i++)
        {
            s_pkt[i] = 0x00;            /* 末包补 0x00 凑满 16 字节 */
        }

        if (!EE_ReadBlock(s_router, s_addr, s_pkt, n))
        {
            Proto_SendAck("RX-ERR");
            s_state = APP_ST_IDLE;
            return;
        }

        Proto_SendR(s_pkt, CFG_PKT_BYTES);

        r = Proto_Wait(s_cmd, CFG_RXOK_TIMEOUT_MS);
        if (r == PROTO_NONE)
        {
            Proto_SendAck("RX-ERR");
            s_state = APP_ST_IDLE;
            return;
        }

        if (str_eq(s_cmd, "RX-OK"))
        {
            /* 正常继续 */
        }
        else if (str_eq(s_cmd, "ABORT"))
        {
            Proto_SendAck("ABORT-OK");
            s_state = APP_ST_IDLE;
            return;
        }
        else
        {
            Proto_SendAck("RX-ERR");
            s_state = APP_ST_IDLE;
            return;
        }

        s_addr += n;
        show_task(1);
        delay_ms(1);                    /* 每包给上位机一点处理时间 */
    }

    Proto_SendData("TX-OVER");
    s_state = APP_ST_IDLE;
}

/* ---------------------------------------------------------------------
 * 分发
 * ------------------------------------------------------------------- */
void App_Init(void)
{
    s_state = APP_ST_IDLE;
    s_router = ROUTER_NONE;
    s_has_edge = 0;
    s_edge = 0;
    s_addr = 0;
}

void App_Handle(const char *cmd)
{
    if (cmd[0] == '\0')
    {
        Proto_SendAck("RX-ERR");
        return;
    }

    if (str_eq(cmd, "TYPE-CAPASA"))
    {
        Proto_SendAck("YES");
        return;
    }

    if (str_eq(cmd, "DIAG"))
    {
        handle_diag();
        return;
    }

    if (str_eq(cmd, "SCAN"))
    {
        handle_scan();
        return;
    }

    if (str_eq(cmd, "PINX"))
    {
        handle_pinx();
        return;
    }

    if (str_eq(cmd, "PINS"))
    {
        handle_pins();
        return;
    }

    if (str_eq(cmd, "UGS"))
    {
        handle_ugs();
        return;
    }

    if (str_eq(cmd, "DIG"))
    {
        handle_dig();
        return;
    }

    if (str_eq(cmd, "GET_!") || str_eq(cmd, "GET"))
    {
        handle_get();
        return;
    }

    if (str_eq(cmd, "INT_W"))
    {
        handle_int_w();
        return;
    }

    if (str_eq(cmd, "INT_R"))
    {
        handle_int_r();
        return;
    }

    if (str_eq(cmd, "TX-OVER"))
    {
        if (s_state == APP_ST_WRITE)
        {
            s_state = APP_ST_IDLE;
            UI_Idle();
        }
        Proto_SendAck("RX-OK");
        return;
    }

    if (str_eq(cmd, "ABORT"))
    {
        s_state = APP_ST_IDLE;
        UI_Idle();
        Proto_SendAck("ABORT-OK");
        return;
    }

    if (str_prefix(cmd, "SET_"))
    {
        handle_set(cmd + 4);
        return;
    }

    if (str_prefix(cmd, "UG_"))
    {
        handle_ug(cmd + 3);
        return;
    }

    if (str_prefix(cmd, "DI_"))
    {
        handle_di(cmd + 3);
        return;
    }

    if (str_prefix(cmd, "ROUTER_"))
    {
        handle_router(cmd + 7);
        return;
    }

    if (str_prefix(cmd, "ADDR_"))
    {
        handle_addr(cmd + 5);
        return;
    }

    if (str_prefix(cmd, "W_"))
    {
        handle_w(cmd + 2);
        return;
    }

    Proto_SendAck("RX-ERR");
}

/* ---- 存储卡状态机（三态）---------------------------------------------
 *   OFFLINE : 24C02 无应答          -> 未插卡        "No card / CARD OFFLINE"
 *   UNINIT  : 24C02 可通讯但
 *             Meta_InfoValid() = 0 -> 未初始化      "Not initialized"
 *   READY   : 可通讯且属性自洽      -> 正常空闲       "Idle / 容量 / 型号"
 *
 * 周期探测 24C02（0x51，属性存储器，插在存储卡上）：卡被拔走就没有应答。
 * 只在空闲态探测（读写会话中不能碰总线）；连续 3 次失败才判离线（防抖），
 * 任何一次成功立即恢复。在线时顺带做一次 Meta_InfoValid 判定。
 * 状态翻转时刷新 OLED（差分，只发变化的页）。
 * 探测间隔用主循环空闲迭代计数粗略节流（约 1s 量级，不需要精确）。
 *
 * ★★ 判据必须与 main / UI_Boot 完全一致，都用 Meta_InfoValid()。
 *    曾经这里写的是"读 0x00 看 bit0"：空白 24C02 读回 0xFF，bit0 = 1，
 *    未初始化的卡被误判成 READY，屏上一秒后就冒出容量和型号 ——
 *    这正是"未初始化卡还显示存储卡属性"的根因。
 * ------------------------------------------------------------------- */
static uint8_t s_card_state = 0xFF;     /* 0xFF = 未知，首次必定触发刷新 */

static void card_state_set(uint8_t st)
{
    if (s_card_state == st)
    {
        return;
    }
    s_card_state = st;

    if (st == CARD_OFFLINE)
    {
        UI_CardState(0);
    }
    else if (st == CARD_UNINIT)
    {
        UI_Uninit();
    }
    else
    {
        UI_CardState(1);            /* UI_Idle + 型号行 */
    }
}

void App_Tick(void)
{
    static uint16_t s_cnt = 0;
    static uint8_t s_miss = 0;

    if (s_state != APP_ST_IDLE)
    {
        return;                     /* 读写会话中不动 IIC2 */
    }

    if (++s_cnt < 20000U)           /* 粗略节流：约 1s 探测一次 */
    {
        return;
    }
    s_cnt = 0;

    if (!I2C2_Probe(ADDR_META_24C02))
    {
        if (++s_miss >= 3)
        {
            s_miss = 0;
            card_state_set(CARD_OFFLINE);
        }
        return;
    }

    s_miss = 0;

    /* 在线：属性缓存没载入过（上电时卡没插 / 没初始化）就先读一次。
     * 不重新读的话，卡是后来才插上的话 g_meta 里还是 main 的默认值，
     * 屏上显示的就是"默认 24C64"这种并不属于这张卡的数据。*/
    if (!g_meta.init_flag)
    {
        if (!Meta_Load())
        {
            return;                 /* 读失败：保持现状，下一轮再试 */
        }
        EE_SetModel((uint16_t)g_meta.model_kbit);
        (void)PCF_SetMask(g_meta.wp_mask);
    }

    if (Meta_InfoValid())
    {
        card_state_set(CARD_READY);
    }
    else
    {
        card_state_set(CARD_UNINIT);
    }
}
