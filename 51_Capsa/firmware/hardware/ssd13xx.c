#include "board.h"
/* ssd13xx.c — SSD1315 128×32 OLED 最小驱动（IIC2 软件 I2C，只支持 8×8 文本）*/
#include "ssd13xx.h"
#include "i2c_sw.h"
#include "font8x8.h"
#include "delay.h"

#define CTRL_CMD        0x00
#define CTRL_DATA       0x40

/* 实际地址：模块 SA0 接法不同，0x3C / 0x3D 各占一半，上电自动探测 */
static uint8_t xdata s_addr = ADDR_OLED;

bit OLED_Found = 0;

/* 整行发送缓冲：控制字节 + 128 列 */
static uint8_t xdata s_row[OLED_W + 1];

/* ---- 差分刷新缓存 -----------------------------------------------------
 * 每页缓存 16 个字符的**期望内容**，重绘时先比较：
 *   · 内容没变 -> 直接返回，一个字节都不发（省掉整页 130 字节的事务）；
 *   · 变了     -> 只重建并刷这一页，其余三页完全不动。
 * 这样写入进度行/进度条/容量行各自独立更新，视觉上不会有整屏闪动，
 * 也不会因为"刷一页"而顺带把别的页重发一遍。
 * 内存：4×16 字符 + 归一化缓冲 + 两个状态字节 = 82 B xdata。
 * ------------------------------------------------------------------- */
static char xdata s_page_txt[OLED_PAGES][16];
static char xdata s_norm[16];
static uint8_t xdata s_bar_pct = 0xFF;      /* 已显示的进度条百分比 */

static bit same16(const char *a, const char *b)
{
    uint8_t i;

    for (i = 0; i < 16; i++)
    {
        if (a[i] != b[i])
        {
            return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * 底层：命令 / 窗口
 * ------------------------------------------------------------------- */
static void oled_cmd2(uint8_t c, uint8_t v)
{
    uint8_t b[3];

    b[0] = CTRL_CMD;
    b[1] = c;
    b[2] = v;
    (void)I2C2_Write(s_addr, b, 3);
}

static void oled_cmd(uint8_t c)
{
    uint8_t b[2];

    b[0] = CTRL_CMD;
    b[1] = c;
    (void)I2C2_Write(s_addr, b, 2);
}

/* 把整页 128 列写出去（s_row[0] 是控制字节）*/
static bit oled_page(uint8_t page)
{
    uint8_t b[4];

    b[0] = CTRL_CMD;
    b[1] = 0x21;                    /* 列地址 0..127 */
    b[2] = 0x00;
    b[3] = (uint8_t)(OLED_W - 1);
    if (!I2C2_Write(s_addr, b, 4))
    {
        return 0;
    }

    b[1] = 0x22;                    /* 页地址 page..page */
    b[2] = (uint8_t)(page & 0x03);
    b[3] = (uint8_t)(page & 0x03);
    if (!I2C2_Write(s_addr, b, 4))
    {
        return 0;
    }

    s_row[0] = CTRL_DATA;
    return I2C2_Write(s_addr, s_row, (uint8_t)(OLED_W + 1));
}

/* ---------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------- */
void OLED_Init(void)
{
    uint8_t i;
    uint8_t t;

    I2C2_Init();
    delay_ms(20);                   /* 上电稳定 */

    /* 地址自适应：写死 0x3C 时，接 0x3D 的屏会完全没反应（全黑）*/
    OLED_Found = 0;
    for (i = 0; i < 2; i++)
    {
        uint8_t a = (uint8_t)(ADDR_OLED + i);   /* 0x3C, 0x3D */

        for (t = 0; t < 2; t++)
        {
            if (I2C2_Probe(a))
            {
                s_addr = a;
                OLED_Found = 1;
                break;
            }
            delay_ms(10);
        }

        if (OLED_Found)
        {
            break;
        }
    }

    if (!OLED_Found)
    {
        return;
    }

    oled_cmd(0xAE);                 /* display off */
    oled_cmd2(0xD5, 0x80);          /* clock divide */
    oled_cmd2(0xA8, 0x1F);          /* multiplex = 32 行 */
    oled_cmd2(0xD3, 0x00);          /* display offset */
    oled_cmd(0x40);                 /* start line 0 */
    oled_cmd2(0x8D, 0x14);          /* charge pump on */
    oled_cmd2(0x20, 0x00);          /* horizontal addressing */
    oled_cmd(0xA1);                 /* segment remap */
    oled_cmd(0xC8);                 /* COM scan reverse */
    oled_cmd2(0xDA, 0x02);          /* COM pins：128×32 sequential */
    oled_cmd2(0x81, 0x8F);          /* contrast */
    oled_cmd2(0xD9, 0xF1);          /* pre-charge */
    oled_cmd2(0xDB, 0x40);          /* VCOMH */
    oled_cmd(0xA4);                 /* 跟随 RAM */
    oled_cmd(0xA6);                 /* 正常显示 */

    /* 清屏：4 页全 0，并把差分缓存同步成"四行空白"，
     * 这样后续第一帧内容与缓存不同，一定会真正刷出去。*/
    for (i = 0; i < OLED_W; i++)
    {
        s_row[i + 1] = 0x00;
    }
    for (i = 0; i < OLED_PAGES; i++)
    {
        (void)oled_page(i);
        for (t = 0; t < 16; t++)
        {
            s_page_txt[i][t] = ' ';
        }
    }
    s_bar_pct = 0xFF;

    oled_cmd(0xAF);                 /* display on */
}

uint8_t OLED_Addr(void)
{
    return OLED_Found ? s_addr : 0x00;
}

/* ---------------------------------------------------------------------
 * 整屏清空
 *
 * 四页各发一次全 0x00。发完把 s_page_txt 全填 0x01（不可打印字符，
 * 永远不会等于任何合法文本），s_bar_pct 复位 0xFF —— 这样后续的
 * OLED_Text / OLED_Bar 一定会重发，不会因为"缓存看起来一样"被跳过。
 *
 * 为什么必须清整屏：卡从"就绪"变成"未初始化/拔走"时，如果只改状态行，
 * 屏上仍留着上一张卡的容量和型号 —— 那是把不存在的数据当属性展示。
 * ------------------------------------------------------------------- */
void OLED_Clear(void)
{
    uint8_t p;
    uint8_t i;

    if (!OLED_Found)
    {
        return;
    }

    for (i = 0; i < OLED_W; i++)
    {
        s_row[i + 1] = 0x00;
    }

    for (p = 0; p < OLED_PAGES; p++)
    {
        (void)oled_page(p);
        for (i = 0; i < 16; i++)
        {
            s_page_txt[p][i] = 0x01;    /* 脏标记：必定不匹配 */
        }
    }

    s_bar_pct = 0xFF;
}

/* ---------------------------------------------------------------------
 * 文本：8×8，最多 16 字符
 * ------------------------------------------------------------------- */
bit OLED_Text(uint8_t page, const char *s)
{
    uint8_t i;
    uint8_t col;

    if (!OLED_Found || page >= OLED_PAGES)
    {
        return 0;
    }

    /* 归一化到 16 字符（不足补空格、超出截断），再与缓存比较 */
    col = 0;
    while (s[col] != 0 && col < 16)
    {
        s_norm[col] = s[col];
        col++;
    }
    while (col < 16)
    {
        s_norm[col++] = ' ';
    }

    if (same16(s_norm, s_page_txt[page]))
    {
        return 1;                   /* 这一页内容没变：一个字节都不发 */
    }

    for (i = 0; i < OLED_W; i++)
    {
        s_row[i + 1] = 0x00;
    }

    for (col = 0; col < 16; col++)
    {
        char c = s_norm[col];
        const unsigned char code *g;

        if (c < ' ' || c > '~')
        {
            c = ' ';
        }

        g = Font8x8[(uint8_t)c - (uint8_t)' '];
        for (i = 0; i < 8; i++)
        {
            s_row[1 + (uint16_t)col * 8U + i] = g[i];
        }
    }

    if (!oled_page(page))
    {
        return 0;                   /* 发送失败：不更新缓存，下次还会重试 */
    }

    for (i = 0; i < 16; i++)
    {
        s_page_txt[page][i] = s_norm[i];
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * 进度条：条区 x 0..95（左右端与填充用 0xFF，上下边框 0x81），
 *         x 96..127 是 4 字符的百分比文字（右对齐，8x8 字形），与条同页。
 * ------------------------------------------------------------------- */
#define BAR_END         95              /* 条区最后一列 */
#define BAR_FILL_MAX    94              /* 内部可填充列数（1..94） */

bit OLED_Bar(uint8_t percent)
{
    uint8_t i;
    uint8_t fill;
    char txt[4];
    uint8_t col;

    if (!OLED_Found)
    {
        return 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    if (percent == s_bar_pct)
    {
        return 1;                   /* 进度条这一格没变：不刷 */
    }

    /* 条区：0..95 */
    fill = (uint8_t)(((uint16_t)percent * BAR_FILL_MAX) / 100U);

    for (i = 0; i <= BAR_END; i++)
    {
        uint8_t v = 0x81;                    /* 只有上下边框 */

        if (i == 0 || i == BAR_END || i <= fill)
        {
            v = 0xFF;
        }
        s_row[i + 1] = v;
    }

    /* 文字区：96..127，4 字符右对齐（" 45%" / "100%"）*/
    if (percent == 100)
    {
        txt[0] = '1';
        txt[1] = '0';
        txt[2] = '0';
    }
    else
    {
        txt[0] = ' ';
        txt[1] = (percent >= 10) ? (char)('0' + percent / 10U) : ' ';
        txt[2] = (char)('0' + percent % 10U);
    }
    txt[3] = '%';

    for (col = 0; col < 4; col++)
    {
        char c = txt[col];
        const unsigned char code *g;

        if (c < ' ' || c > '~')
        {
            c = ' ';
        }

        g = Font8x8[(uint8_t)c - (uint8_t)' '];
        for (i = 0; i < 8; i++)
        {
            s_row[1 + 96U + (uint16_t)col * 8U + i] = g[i];
        }
    }

    if (!oled_page(OLED_PAGE_BAR))
    {
        return 0;
    }

    s_bar_pct = percent;
    return 1;
}
