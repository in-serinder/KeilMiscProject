#include "board.h"
/* ui.c — OLED 显示层（128×32，四行 8×8）*/
#include "ui.h"
#include "ssd13xx.h"
#include "i2c_sw.h"
#include "meta.h"
#include "ee.h"

static char xdata s_line[17];       /* 16 字符 + '\0' */
static uint8_t s_last_pct = 0xFF;

/* ★ 门闩：1 = 卡已初始化且属性有效，允许显示容量/型号/进度条；
 *   0 = 未插卡或未初始化，一切"存储卡属性"都不许上屏。
 *   由 UI_CardState() / UI_Uninit() / UI_Boot() / UI_SetCardReady() 维护。
 *   所有绘制属性的入口（line_model / UI_Capacity / UI_Progress / UI_Idle）
 *   开头都检查它 —— 这是唯一防线，任何调用路径都绕不过去。*/
static uint8_t s_card_ok = 0;

/* ---------------------------------------------------------------------
 * 迷你格式化（不引入 printf）
 * ------------------------------------------------------------------- */
static uint8_t put_u16(char *p, uint16_t v)
{
    uint8_t d[5];
    uint8_t n = 0;
    uint8_t i;

    do
    {
        d[n++] = (uint8_t)(v % 10U);
        v = (uint16_t)(v / 10U);
    } while (v != 0);

    for (i = 0; i < n; i++)
    {
        p[i] = (char)('0' + d[n - 1 - i]);
    }
    return n;
}

/* 字节数 -> "12.3"（0.1KB 精度，16 位运算）*/
static uint8_t put_kb(char *p, uint32_t bytes)
{
    uint16_t w = (uint16_t)((bytes * 10UL) >> 10);   /* 0.1KB 单位 */
    uint8_t n = put_u16(p, (uint16_t)(w / 10U));

    p[n++] = '.';
    p[n++] = (char)('0' + (uint8_t)(w % 10U));
    return n;
}

/* 左对齐写满 16 字符，右侧补空格 */
static void line_set(const char *s)
{
    uint8_t i = 0;

    while (*s != 0 && i < 16)
    {
        s_line[i++] = *s++;
    }
    while (i < 16)
    {
        s_line[i++] = ' ';
    }
    s_line[16] = 0;
}

/* 在 at 列写 "NN%"，并截断（at = 12 时 100% 也不会越界）*/
static void line_pct(uint8_t at, uint8_t percent)
{
    uint8_t n = put_u16(&s_line[at], percent);

    s_line[at + n] = '%';
    s_line[at + n + 1] = 0;
}

/* 介质名（与上位机 StorageMediumCatalog 的显示名一致，索引 = g_meta.medium）*/
static const char code *code MED_NAMES[4] =
{
    "EEPROM", "FRAM", "EERAM", "SRAM"
};

/* 可写片数 = 8 - popcount(wp_mask)。WP 片不在容量/型号行里出现 */
static uint8_t writable_chips(void)
{
    uint8_t v = g_meta.wp_mask;
    uint8_t n = 8;

    while (v != 0U)
    {
        if ((v & 1U) != 0U)
        {
            n--;
        }
        v >>= 1;
    }

    return n;
}

/* 页 3：型号 + 可写片数 + 存储介质，例 "24C128x6 EEPROM"（最长 16 字符）。
 * ★ x 后面是**可写片数**，不是固定 8：WP 掉的片不算容量，也不在这里出现。*/
static void line_model(void)
{
    uint8_t n = 0;
    uint8_t med;
    const char code *p;

    if (!s_card_ok)
    {
        return;                     /* 未就绪：绝不上屏型号，哪怕上层误调用 */
    }

    s_line[n++] = '2';
    s_line[n++] = '4';
    s_line[n++] = 'C';
    n = (uint8_t)(n + put_u16(&s_line[n], EE_GetModel()));
    s_line[n++] = 'x';
    n = (uint8_t)(n + put_u16(&s_line[n], writable_chips()));
    s_line[n++] = ' ';

    med = g_meta.medium;
    if (med > 3)
    {
        med = 0;                    /* 越界按 EEPROM 显示 */
    }
    p = MED_NAMES[med];
    while (*p != 0 && n < 16)
    {
        s_line[n++] = *p++;
    }
    while (n < 16)
    {
        s_line[n++] = ' ';
    }
    s_line[16] = 0;
    (void)OLED_Text(OLED_PAGE_EXTRA, s_line);
}

/* ---------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------- */
void UI_Init(void)
{
    OLED_Init();
    s_last_pct = 0xFF;
    s_card_ok = 0;                  /* 上电默认关门：判定通过前不显示任何属性 */
}

/* 卡"就绪/不可用"门闩的外部开关（SET 成功、状态机判定后调用）*/
void UI_SetCardReady(bit ready)
{
    s_card_ok = ready ? 1 : 0;
}

void UI_Boot(bit iic2_pins_ok, uint8_t scan_mask)
{
    scan_mask = scan_mask;          /* 参数保留（接口兼容），状态判定已改用 24C02 应答 */

    if (!OLED_Found)
    {
        return;
    }

    s_last_pct = 0xFF;
    s_card_ok = 0;                  /* 先关门，下面判定通过才允许显示属性 */

    if (!iic2_pins_ok)
    {
        line_set("I2C2 PIN ERR");
        (void)OLED_Text(OLED_PAGE_STATUS, s_line);
        return;
    }

    /* 三态判定（与 App_Tick 的周期检测同口径，都用 Meta_InfoValid）：
     *   24C02 不应答              -> 未插卡
     *   应答但属性不完整/非法     -> 未初始化（提示上位机先初始化）
     *   应答且属性自洽            -> 正常就绪
     * ★ 判据必须是 Meta_InfoValid()，不能只看 init_flag 的某一位：
     *   空白 24C02 读回来是 0xFF，0xFF & 0x01 也是 1。*/
    if (!I2C2_Probe(ADDR_META_24C02))
    {
        UI_CardState(0);            /* 未插卡：整屏清空 + 提示 */
        return;
    }

    if (!Meta_InfoValid())
    {
        UI_Uninit();
        return;
    }

    s_card_ok = 1;
    line_set("Capsa ready");
    (void)OLED_Text(OLED_PAGE_STATUS, s_line);
    line_model();

    UI_Capacity(g_meta.used, Meta_ArrayBytes());
}

void UI_Idle(void)
{
    /* 未就绪时"空闲"这两个字本身也不该盖掉"未初始化/未插卡"提示：
     * ABORT、TX-OVER 这些路径都会调到这里。*/
    if (!OLED_Found || !s_card_ok)
    {
        return;
    }

    s_last_pct = 0xFF;
    line_set("Idle");
    (void)OLED_Text(OLED_PAGE_STATUS, s_line);
    UI_Capacity(g_meta.used, Meta_ArrayBytes());
}

void UI_Message(const char *line16)
{
    if (!OLED_Found)
    {
        return;
    }

    line_set(line16);
    (void)OLED_Text(OLED_PAGE_STATUS, s_line);
}

void UI_Task(bit is_read, uint8_t chip7, uint8_t percent)
{
    uint8_t n;

    if (!OLED_Found)
    {
        return;
    }

    /* "Read 0x50"（9 字符）/ "Write 0x50"（10 字符）*/
    if (is_read)
    {
        s_line[0] = 'R';
        s_line[1] = 'e';
        s_line[2] = 'a';
        s_line[3] = 'd';
        s_line[4] = ' ';
        n = 5;
    }
    else
    {
        s_line[0] = 'W';
        s_line[1] = 'r';
        s_line[2] = 'i';
        s_line[3] = 't';
        s_line[4] = 'e';
        s_line[5] = ' ';
        n = 6;
    }

    s_line[n++] = '0';
    s_line[n++] = 'x';
    {
        const char code HEX[] = "0123456789ABCDEF";
        s_line[n++] = HEX[(chip7 >> 4) & 0x0F];
        s_line[n++] = HEX[chip7 & 0x0F];
    }

    while (n < 12)
    {
        s_line[n++] = ' ';
    }
    s_line[12] = 0;

    if (percent > 100)
    {
        percent = 100;
    }
    line_pct(12, percent);

    (void)OLED_Text(OLED_PAGE_STATUS, s_line);
    /* ★ 进度条固定显示容量占用，不随任务百分比走：
     *   任务进度只在页 0 的百分比上体现，写入完成后由 UG 上报新占用，
     *   UI_Capacity() 才会重绘进度条。任务进行中它保持上一次的占用值。*/
}

/* 元数据变化后刷新型号行 + 容量行（SET 成功、型号切换时调用）。
 * 没有它，SET 把卡改成 24C128 后屏上还停在 "24C64 x8"，要重启才对。*/
void UI_RefreshMeta(void)
{
    if (!OLED_Found || !s_card_ok)
    {
        return;
    }

    line_model();
    UI_Capacity(g_meta.used, Meta_ArrayBytes());
}

/* 存储卡在位状态：主循环周期探测 24C02 应答，状态翻转时调用。
 * 离线 -> 整屏清空 + "No card"；恢复 -> 回到正常空闲显示。
 * 差分缓存保证只发内容真正变化的页。*/
void UI_CardState(bit present)
{
    if (!OLED_Found)
    {
        return;
    }

    if (!present)
    {
        s_card_ok = 0;              /* ★ 关门：之后任何属性绘制都被拦掉 */
        s_last_pct = 0xFF;
        OLED_Clear();               /* ★ 抹掉上一张卡留下的容量/型号/进度条 */

        line_set("No card");
        (void)OLED_Text(OLED_PAGE_STATUS, s_line);
        line_set("Insert card");
        (void)OLED_Text(OLED_PAGE_BAR, s_line);
        line_set("Capacity unknown");
        (void)OLED_Text(OLED_PAGE_INFO, s_line);
        line_set("CARD OFFLINE");
        (void)OLED_Text(OLED_PAGE_EXTRA, s_line);
    }
    else
    {
        s_card_ok = 1;
        UI_Idle();
        line_model();
    }
}

/* 卡在位（24C02 / PCF8574 可通讯）但属性数据不完整或非法（Meta_InfoValid = 0）：
 * 上位机需要先"初始化存储卡"，之后才会进入正常的空闲显示。
 * ★ 这里是"未初始化卡不许显示属性"的最后一道闸：
 *   整屏先清空，再只写四行提示，屏上不会剩下任何一个字节的容量/型号。*/
void UI_Uninit(void)
{
    if (!OLED_Found)
    {
        return;
    }

    s_card_ok = 0;
    s_last_pct = 0xFF;

    OLED_Clear();                   /* ★ 彻底清掉上一屏 */

    line_set("Not initialized");
    (void)OLED_Text(OLED_PAGE_STATUS, s_line);
    line_set("Init in Capsa");
    (void)OLED_Text(OLED_PAGE_BAR, s_line);
    line_set("Capacity unknown");
    (void)OLED_Text(OLED_PAGE_INFO, s_line);
    line_set("UNINITIALIZED");
    (void)OLED_Text(OLED_PAGE_EXTRA, s_line);
}

void UI_Progress(uint8_t percent)
{
    if (!OLED_Found || !s_card_ok)
    {
        return;                     /* 未就绪：进度条（容量占用）不许上屏 */
    }
    if (percent > 100)
    {
        percent = 100;
    }

    /* 2% 步进节流。驱动层已做差分（内容没变的页一个字节都不发），
     * 所以这里的节流只用来限制"整页重建"的次数；50 级的变化在 128 像素
     * 宽的进度条上已经是逐格推进，视觉上平滑，单步开销约 6ms。*/
    if (s_last_pct != 0xFF && (uint8_t)(percent / 2U) == (uint8_t)(s_last_pct / 2U))
    {
        return;
    }
    s_last_pct = percent;

    (void)OLED_Bar(percent);
}

void UI_Capacity(uint32_t usedBytes, uint32_t totalBytes)
{
    uint8_t n;
    uint8_t pct;

    if (!OLED_Found || !s_card_ok || totalBytes == 0)
    {
        return;                     /* 未就绪：容量行与进度条一律不许上屏 */
    }
    if (usedBytes > totalBytes)
    {
        usedBytes = totalBytes;
    }

    /* 页 2：只放容量数字（"63.9/64.0KB"）—— 占用百分比已移到
     * 进度条那一行的末尾（OLED_Bar 内部绘制），本行不再重复。*/
    line_set("");
    n = put_kb(s_line, usedBytes);
    s_line[n++] = '/';
    n = (uint8_t)(n + put_kb(&s_line[n], totalBytes));
    s_line[n++] = 'K';
    s_line[n++] = 'B';

    pct = (uint8_t)((usedBytes * 100UL) / totalBytes);

    (void)OLED_Text(OLED_PAGE_INFO, s_line);

    s_last_pct = 0xFF;
    UI_Progress(pct);
}
