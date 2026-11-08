#include "board.h"
/* meta.c — 24C02 元数据读写实现 */
#include "meta.h"
#include "ee.h"
#include "delay.h"

meta_t xdata g_meta;

/* C51 SMALL 模式下局部数组落在 128 B 的 data 区，48 字节就吃不消，
 * 统一改成文件级 xdata 缓冲 */
static uint8_t xdata s_buf[48];
static uint8_t xdata s_zero[10];

/* ---------------------------------------------------------------------
 * 小端整数读写
 * ------------------------------------------------------------------- */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ---------------------------------------------------------------------
 * 检测区：全 0xFF = 从未初始化；其它（含全 0x00）= 已初始化
 * ------------------------------------------------------------------- */
bit Meta_IsInitialized(void)
{
    uint8_t i;

    if (!M24_Read(META_DETECT, s_zero, META_DETECT_LEN))
    {
        return 0;                   /* 读不到就当未初始化（会让上位机走 SET）*/
    }

    for (i = 0; i < META_DETECT_LEN; i++)
    {
        if (s_zero[i] != 0xFF)
        {
            return 1;               /* 只要有一个字节不是 0xFF 就算已初始化 */
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------
 * 属性数据"可展示"判定
 *
 * ★ 这是屏幕能不能显示容量/型号的唯一依据。之前主循环用
 *   "读 0x00 是否 bit0 = 1" 判定，空白的 24C02 读回来是 0xFF，
 *   0xFF & 0x01 == 1 -> 未初始化的卡被当成已初始化，
 *   屏上就冒出 64.0KB / 24C64x8 这些根本不存在的属性。
 *
 *   判据必须同时满足：
 *     1) 检测区（0xF6~0xFF）不是全 0xFF —— 卡确实被初始化过；
 *     2) init_flag == 0x01              —— 属性字段被完整写过；
 *     3) medium <= 3                    —— 介质枚举合法；
 *     4) model_kbit 在已知型号表内      —— 不是 0xFFFF/0xFFFFFFFF 之类的空白值；
 *     5) cap_kb 落在 1..4096            —— 容量有意义。
 * ------------------------------------------------------------------- */
bit Meta_InfoValid(void)
{
    if (!Meta_IsInitialized())
    {
        return 0;                   /* 检测区全 0xFF：从没初始化过 */
    }

    if (g_meta.init_flag != 0x01)
    {
        return 0;                   /* 半初始化 / 字段没写全 */
    }

    if (g_meta.medium > 3)
    {
        return 0;
    }

    /* C51 对 switch(uint32) 生成的代码又大又慢，这里用 if 链 */
    if (g_meta.model_kbit != (uint32_t)EE_MODEL_24C32 &&
        g_meta.model_kbit != (uint32_t)EE_MODEL_24C64 &&
        g_meta.model_kbit != (uint32_t)EE_MODEL_24C128 &&
        g_meta.model_kbit != (uint32_t)EE_MODEL_24C256 &&
        g_meta.model_kbit != (uint32_t)EE_MODEL_24C512 &&
        g_meta.model_kbit != (uint32_t)EE_MODEL_24C1024)
    {
        return 0;                   /* 0xFFFF / 0xFFFFFFFF 等空白值 */
    }

    if (g_meta.cap_kb == 0 || g_meta.cap_kb > 4096U)
    {
        return 0;
    }

    return 1;
}

bit Meta_Load(void)
{    uint8_t i;

    /* 0x00 ~ 0x2B 一次读入（48 字节）*/
    if (!M24_Read(META_INIT_FLAG, s_buf, 48))
    {
        return 0;
    }

    g_meta.init_flag = s_buf[META_INIT_FLAG];
    g_meta.wp_mask = s_buf[META_WP_MASK];
    g_meta.health_mask = s_buf[META_HEALTH_MASK];

    for (i = 0; i < META_NAME_LEN; i++)
    {
        g_meta.name[i] = (char)s_buf[META_NAME + i];
    }
    g_meta.name[META_NAME_LEN] = '\0';

    g_meta.cap_kb = rd16(&s_buf[META_CAP_KB]);
    g_meta.mtime = rd32(&s_buf[META_MTIME]);
    g_meta.rtime = rd32(&s_buf[META_RTIME]);
    g_meta.medium = s_buf[META_MEDIUM];
    g_meta.model_kbit = rd32(&s_buf[META_MODEL]);

    for (i = 0; i < META_UID_LEN; i++)
    {
        g_meta.uid[i] = s_buf[META_UID + i];
    }

    /* 已使用空间 6 字节（高 2 字节忽略，本机容量远小于 4GB）*/
    {
        uint8_t u[6];
        if (M24_Read(META_USED, u, 6))
        {
            g_meta.used = (uint32_t)u[0] | ((uint32_t)u[1] << 8) |
                          ((uint32_t)u[2] << 16) | ((uint32_t)u[3] << 24);
        }
        else
        {
            g_meta.used = 0;
        }
    }

    /* 哈希 + 写入次数 */
    {
        if (M24_Read(META_HASH, s_buf, 12))
        {
            for (i = 0; i < META_HASH_LEN; i++)
            {
                g_meta.hash[i] = s_buf[i];
            }
            g_meta.write_count = rd32(&s_buf[META_HASH_LEN]);
        }
        else
        {
            for (i = 0; i < META_HASH_LEN; i++)
            {
                g_meta.hash[i] = 0;
            }
            g_meta.write_count = 0;
        }
    }

    return 1;
}

/* ---------------------------------------------------------------------
 * 首次初始化：写 0x00~0x2B（字段）+ 0x2C~0x31 清零 + 0x3E~0xF5 清零
 *              + 检测区写 0x00。DI 区只在"全 0xFF / 全 0x00"时规范成 0。
 * ------------------------------------------------------------------- */
bit Meta_FirstSet(void)
{
    uint8_t i;

    /* 组装 0x00~0x2B */
    s_buf[META_INIT_FLAG] = 0x01;
    s_buf[META_WP_MASK] = g_meta.wp_mask;
    s_buf[META_HEALTH_MASK] = g_meta.health_mask;

    for (i = 0; i < META_NAME_LEN; i++)
    {
        s_buf[META_NAME + i] = (uint8_t)g_meta.name[i];
    }
    wr16(&s_buf[META_CAP_KB], g_meta.cap_kb);
    wr32(&s_buf[META_MTIME], g_meta.mtime);
    wr32(&s_buf[META_RTIME], g_meta.rtime);
    s_buf[META_MEDIUM] = g_meta.medium;
    wr32(&s_buf[META_MODEL], g_meta.model_kbit);
    for (i = 0; i < META_UID_LEN; i++)
    {
        s_buf[META_UID + i] = g_meta.uid[i];
    }

    if (!M24_Write(META_INIT_FLAG, s_buf, 44))
    {
        return 0;
    }

    /* 已使用空间清零 */
    for (i = 0; i < 6; i++)
    {
        s_zero[i] = 0x00;
    }
    g_meta.used = 0;
    if (!M24_Write(META_USED, s_zero, 6))
    {
        return 0;
    }

    /* DI 区：出厂空白才规范成 0，已有记录原样保留（寿命计数不许清除）*/
    {
        bit blank = 1;
        bit all_ff = 1;

        if (M24_Read(META_HASH, s_buf, 12))
        {
            for (i = 0; i < 12; i++)
            {
                if (s_buf[i] != 0x00)
                {
                    blank = 0;
                }
                if (s_buf[i] != 0xFF)
                {
                    all_ff = 0;
                }
            }

            if (blank || all_ff)
            {
                for (i = 0; i < 12; i++)
                {
                    s_buf[i] = 0x00;
                }
                if (!M24_Write(META_HASH, s_buf, 12))
                {
                    return 0;
                }
                for (i = 0; i < META_HASH_LEN; i++)
                {
                    g_meta.hash[i] = 0;
                }
                g_meta.write_count = 0;
            }
        }
    }

    /* 预留区 0x3E~0xF5 清零（184 B，分两段写）*/
    for (i = 0; i < 10; i++)
    {
        s_zero[i] = 0x00;
    }
    for (i = 0; i < 19; i++)
    {
        if (!M24_Write((uint8_t)(META_RESERVED + i * 10), s_zero, 10))
        {
            return 0;
        }
    }

    /* 检测区写 0x00 → 标记已初始化 */
    return Meta_SaveDetect();
}

bit Meta_SaveInfo(void)
{
    uint8_t i;

    s_buf[META_INIT_FLAG] = (uint8_t)(g_meta.init_flag ? 0x01 : 0x00);
    s_buf[META_WP_MASK] = g_meta.wp_mask;
    s_buf[META_HEALTH_MASK] = g_meta.health_mask;

    for (i = 0; i < META_NAME_LEN; i++)
    {
        s_buf[META_NAME + i] = (uint8_t)g_meta.name[i];
    }
    wr16(&s_buf[META_CAP_KB], g_meta.cap_kb);
    wr32(&s_buf[META_MTIME], g_meta.mtime);
    wr32(&s_buf[META_RTIME], g_meta.rtime);
    s_buf[META_MEDIUM] = g_meta.medium;
    wr32(&s_buf[META_MODEL], g_meta.model_kbit);
    for (i = 0; i < META_UID_LEN; i++)
    {
        s_buf[META_UID + i] = g_meta.uid[i];
    }

    return M24_Write(META_INIT_FLAG, s_buf, 44);
}

bit Meta_SaveUsed(void)
{
    uint8_t u[6];

    u[0] = (uint8_t)(g_meta.used & 0xFF);
    u[1] = (uint8_t)((g_meta.used >> 8) & 0xFF);
    u[2] = (uint8_t)((g_meta.used >> 16) & 0xFF);
    u[3] = (uint8_t)((g_meta.used >> 24) & 0xFF);
    u[4] = 0;
    u[5] = 0;

    return M24_Write(META_USED, u, 6);
}

bit Meta_SaveDataInfo(void)
{
    uint8_t i;

    for (i = 0; i < META_HASH_LEN; i++)
    {
        s_buf[i] = g_meta.hash[i];
    }
    wr32(&s_buf[META_HASH_LEN], g_meta.write_count);

    return M24_Write(META_HASH, s_buf, 12);
}

bit Meta_SaveDetect(void)
{
    uint8_t i;

    for (i = 0; i < META_DETECT_LEN; i++)
    {
        s_zero[i] = 0x00;
    }

    return M24_Write(META_DETECT, s_zero, META_DETECT_LEN);
}

void Meta_SetUsed(uint32_t used)
{
    g_meta.used = used;
}

/* ---------------------------------------------------------------------
 * 有效容量：可写片数 × 单片容量
 *
 * WP 片被 PCF8574 硬件拉高、写不进去，它们不属于可用容量。
 * 容量行、进度条基准、UG 的上限、cap_kb 字段都从这里来。
 * 以前用"8 片 × 单片"：点了一个 WP，屏上的容量纹丝不动 —— 是虚的。
 * ------------------------------------------------------------------- */
uint32_t Meta_ArrayBytes(void)
{
    uint8_t v = g_meta.wp_mask;
    uint8_t writable = 8;

    while (v != 0U)
    {
        if ((v & 1U) != 0U)
        {
            writable--;
        }
        v >>= 1;
    }

    if (writable == 0U)
    {
        return 0;
    }

    return (uint32_t)EE_ChipBytes() * writable;
}

/* 按当前型号 + WP 掩码重算 cap_kb（KB，向下取整）。
 * ★ SET 命令处理完字段后必须调用：WP 点击的 SET 其它字段全是 '!' 占位，
 *   只有这里能把容量从"8 片口径"修正成"可写片口径"。 */
void Meta_RecalcCapacity(void)
{
    g_meta.cap_kb = (uint16_t)(Meta_ArrayBytes() / 1024UL);
}

void Meta_SetDataInfo(bit has_hash, const uint8_t *hash8, bit has_count, uint32_t count)
{
    uint8_t i;

    if (has_hash)
    {
        for (i = 0; i < META_HASH_LEN; i++)
        {
            g_meta.hash[i] = hash8[i];
        }
    }
    if (has_count)
    {
        g_meta.write_count = count;
    }
}
