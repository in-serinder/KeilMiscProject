/* =====================================================================
 * TM1637.c — 四位数码管驱动（起始/停止/写字节逻辑沿用原工程已验证版本）
 *
 * 命令字：
 *   0x40  数据命令：地址自动加一，写显存
 *   0xC0  地址命令：从第 1 位开始写
 *   0x88|亮度  显示控制：开显示 + 亮度(0~7)
 *
 * 说明：原来 TM1637_StringDisplay 里调用的 charToint() 从来没有定义过，
 *       这里改成 TM1637_CharToSeg() 统一做字符到段码的映射。
 * ===================================================================== */
#include "TM1637.h"
#include "delay.h"

#define TM_DLY()    Delay_us(CFG_TM1637_DLY_US)

/* ---------------- 底层时序 ---------------- */
static void tm_start(void)
{
    TM1637_CLK = 1;
    TM1637_DIO = 1;
    TM_DLY();
    TM1637_DIO = 0;         /* CLK 高时 DIO 下降沿 = 起始 */
    TM_DLY();
}

static void tm_stop(void)
{
    TM1637_CLK = 0;
    TM_DLY();
    TM1637_DIO = 0;
    TM_DLY();
    TM1637_CLK = 1;
    TM_DLY();
    TM1637_DIO = 1;         /* CLK 高时 DIO 上升沿 = 停止 */
    TM_DLY();
}

/* 低位在前，第 9 个时钟只给脉冲，不读 ACK */
static void tm_write(uint8_t byte)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        TM1637_CLK = 0;
        TM_DLY();
        TM1637_DIO = (byte & 0x01) ? 1 : 0;
        TM_DLY();
        byte >>= 1;
        TM1637_CLK = 1;
        TM_DLY();
    }

    TM1637_CLK = 0;
    TM1637_DIO = 1;
    TM_DLY();
    TM1637_CLK = 1;
    TM_DLY();
    TM1637_CLK = 0;
}

/* ---------------- 对外 ---------------- */
void TM1637_SetBright(uint8_t level)
{
    if (level > 7)
    {
        level = 7;
    }
    tm_start();
    tm_write((uint8_t)(0x88 | level));      /* 开显示 + 亮度 */
    tm_stop();
}

void TM1637_WriteSegs(const uint8_t *seg, uint8_t n)
{
    uint8_t i;

    tm_start();
    tm_write(0x40);                         /* 自动地址递增 */
    tm_stop();

    tm_start();
    tm_write(0xC0);                         /* 起始地址 0 */
    for (i = 0; i < n; i++)
    {
        tm_write(seg[i]);
    }
    tm_stop();
}

void TM1637_Clear(void)
{
    const uint8_t seg[4] = { 0x00, 0x00, 0x00, 0x00 };

    TM1637_WriteSegs(seg, 4);
}

void TM1637_Init(void)
{
    TM1637_CLK = 1;
    TM1637_DIO = 1;
    Delay_ms(2);

    TM1637_SetBright(CFG_TM1637_BRIGHT);
    TM1637_Clear();
}

uint8_t TM1637_CharToSeg(char c)
{
    switch (c)
    {
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;

    case 'A': case 'a': return 0x77;
    case 'B': case 'b': return 0x7C;
    case 'C': case 'c': return 0x39;
    case 'D': case 'd': return 0x5E;
    case 'E': case 'e': return 0x79;
    case 'F': case 'f': return 0x71;

    case 'I': case 'i': return 0x06;    /* 一根竖线 */
    case 'L': case 'l': return 0x38;
    case 'O': case 'o': return 0x5C;
    case 'R': case 'r': return 0x50;    /* 只亮 e+g 两段，像杂点，状态码里已不再使用 */
    case 'W': case 'w': return 0x3E;    /* 七段画不出 W，只能用 U 形代替 */
    case 'U': case 'u': return 0x3E;    /* 0x3E = b,c,d,e,f，就是 "U" */
    case 'n':           return 0x54;
    case 'P': case 'p': return 0x73;
    case 'S': case 's': return 0x6D;    /* 与 '5' 同形 */
    case 't':           return 0x78;
    case 'H': case 'h': return 0x76;
    case 'y':           return 0x6E;

    case '-':           return 0x40;
    case ' ':           return 0x00;
    default:            return 0x00;
    }
}

void TM1637_Show4(const char *s, bit colon)
{
    uint8_t seg[4];
    uint8_t i;

    for (i = 0; i < 4; i++)
    {
        seg[i] = TM1637_CharToSeg(s[i]);
    }
    if (colon)
    {
        seg[2] |= 0x80;         /* 模块第 2 位带小数点 */
    }
    TM1637_WriteSegs(seg, 4);
}
