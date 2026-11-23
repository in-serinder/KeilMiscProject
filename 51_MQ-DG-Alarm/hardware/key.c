/* =====================================================================
 * key.c  —  TM1637 片内键扫读键 + 消抖
 *
 * 为什么不用 GPIO 读键：STC8G1K08A 8 脚封装只剩 P3.0/P3.1（UART），
 * 而 A/K1/K2 是三根线；数码管模块的 A 实际接在 TM1637 的某条 SEG 上，
 * 所以只能走芯片自带的键扫。
 *
 * ------------------ 42H 读回的键值格式（数据手册，低电平有效）------------------
 *   没按键    : 0xFF
 *   b4 = 0    : K1 行的某个键按下，b7~b5 用"低有效 3 位"编码是第几条 SEG
 *   b3 = 0    : K2 行的某个键按下，b7~b5 同上
 *   b2 ~ b0   : 恒为 1
 *   例：A 接 SEG1 时 add(A-K1)=0xEF、sub(A-K2)=0xF7；
 *       A 接 SEG8 时 add=0x0F、sub=0x17。
 *
 * ★ 判"哪一行"只看 b4/b3，与 A 接在哪条 SEG 上无关，所以 add/sub 的区分天然稳定。
 * ★ 陷阱：add(0xEF) 和 sub(0xF7) 的 bit0 都是 1。若按"某一位为 1"的位掩码去判键，
 *   两个键会一起命中同一个掩码 —— 表现就是"按 sub 也在加 10"。必须按上面的行位判。
 * ★ TM1637 不支持多键同时按下（手册明确说明），所以一次只会有一个有效码。
 *
 * 若模块的 A 其实是 GND、K1/K2 引到单片机，则把 board.h 的
 * CFG_KEY_GPIO_MODE 置 1，改走 P3.0/P3.1 直读。
 * ===================================================================== */
#include "key.h"
#include "TM1637.h"

#define CFG_KEY_DEBOUNCE        1           /* 连续 2 次(20ms)一致才认 */
#define CFG_TM_KEY_RD_BYTES     2           /* 多读一个字节，兼容按 GRID 分字节的模块 */

/* 行选择位（在取反后的字节里为 1 表示该位是 0） */
#define TM_KEY_ROW_K1           0x10        /* b4 = 0 -> K1 行 -> add */
#define TM_KEY_ROW_K2           0x08        /* b3 = 0 -> K2 行 -> sub */

static uint16_t s_raw;                      /* 最近一次读回的两个原始字节（自检用） */
static uint8_t  s_state;                    /* 上一次解码后的键状态 */
static uint8_t  s_stable;                   /* 消抖后的键状态 */
static uint8_t  s_cnt;

/* 把一个键扫字节解成 KEY_ADD / KEY_SUB / 0。
 * 用"b2~b0 必须全 1"做合法性筛选：0xFF(无键)、0x00(总线悬空)、
 * 多键或按 GRID 分字节的其它码型都会被这条挡掉，不会误判成 add。 */
#if !CFG_KEY_GPIO_MODE
static uint8_t tm_key_decode(uint8_t b)
{
    uint8_t z;

    z = (uint8_t)~b;

    if ((z & 0x07) != 0)
    {
        return 0;
    }
    if ((z & TM_KEY_ROW_K1) != 0)
    {
#if CFG_KEY_SWAP
        return KEY_SUB;             /* 模块上 K1/K2 与丝印反了：交换 */
#else
        return KEY_ADD;
#endif
    }
    if ((z & TM_KEY_ROW_K2) != 0)
    {
#if CFG_KEY_SWAP
        return KEY_ADD;
#else
        return KEY_SUB;
#endif
    }
    return 0;
}
#endif /* !CFG_KEY_GPIO_MODE */

static uint8_t key_scan(void)
{
#if CFG_KEY_GPIO_MODE
    /* 直读模式：A 接 GND，按下时 K 线被拉低。
     * P3.0/P3.1 复位后是准双向口，写 1 即有内部弱上拉，可直接读。 */
    uint8_t k = 0;

    s_raw = 0;
    if (KEY_ADD_PIN == 0)
    {
        k |= KEY_ADD;
        s_raw |= KEY_ADD;
    }
    if (KEY_SUB_PIN == 0)
    {
        k |= KEY_SUB;
        s_raw |= KEY_SUB;
    }
    return k;
#else
    uint8_t buf[2];
    uint8_t k;

    buf[0] = 0xFF;
    buf[1] = 0xFF;
    TM1637_ReadKeys(buf, CFG_TM_KEY_RD_BYTES);

    s_raw = (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);

    k = tm_key_decode(buf[0]);
    if (k == 0)
    {
        k = tm_key_decode(buf[1]);      /* 有的模块按键码落在后面的字节里 */
    }
    return k;
#endif
}

#if CFG_KEY_RAW_SHOW || CFG_DEBUG_UART
uint16_t Key_LastRaw(void)
{
    return s_raw;
}
#endif

uint8_t Key_Update(void)
{
    uint8_t now;

    now = key_scan();

    if (now == s_state)
    {
        if (s_cnt < 250)
        {
            s_cnt++;
        }
    }
    else
    {
        s_state = now;
        s_cnt = 0;
    }

    if (s_cnt >= CFG_KEY_DEBOUNCE)
    {
        s_stable = s_state;
    }

    return s_stable;
}
