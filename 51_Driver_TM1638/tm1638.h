#ifndef __TM1638_H__
#define __TM1638_H__

#include "stc89c52.c"

/* ==========================================================================
 * TM1638  8 位数码管 + 8 按键 + 8 LED 模块（三线串行）
 * --------------------------------------------------------------------------
 * 接线：
 *   STB  P20   片选/帧同步，低有效
 *   CLK  P21   时钟，数据在上升沿被锁存
 *   DIO  P22   数据，双向（准双向口：写 1 后即可读）
 *
 * 协议要点（很容易写反，务必记住）：
 *   1. 三线串行是 **LSB 在前**，不是常见的 MSB 在前；
 *   2. 一次完整事务由 STB 下降沿开始、STB 上升沿结束；
 *   3. 显示 RAM 共 16 字节，地址 0xC0~0xCF：
 *        偶数地址 0xC0+2n = 第 n 个数码管的段码（bit0=a … bit6=g，bit7=dp）
 *        奇数地址 0xC1+2n = 第 n 位上的 LED（常见模块用 bit0，即 SEG9）
 *   4. 读按键：发 0x42 后连读 4 字节。这 4 字节**不是顺序排开**，而是一个
 *        2 行 × 4 列矩阵：字节 i = 第 i+1 列，两行分别落在该字节的 bit0 和 bit4。
 *      **行位不是 bit0/bit1，而是 bit0/bit4**（本模块实测，见下面 TM_KEY_ROW2）：
 *        按 S1~S4 -> 出现在字节 0~3 的 bit0
 *        按 S5~S8 -> 出现在字节 0~3 的 bit4
 *      两个坑：
 *        a. 若按"顺序映射"（byte0 bit0=1, bit1=2, byte1 bit0=3 …）去解，
 *           按 S1/S2/S3/S4 会错报成 S1/S3/S5/S7；
 *        b. 若按"K1=bit0 / K2=bit1"去解，S5~S8 会完全读不到，看着像后四个键坏了。
 *      两者都不是硬件问题，改映射即可。
 * ========================================================================== */

#define TM_STB          P20
#define TM_CLK          P21
#define TM_DIO          P22

#define TM_DIGITS       8
#define TM_REG_BYTES    16

/* 命令字 */
#define TM_CMD_DATA_AUTO    0x40    /* 自动地址递增写显示 RAM */
#define TM_CMD_DATA_FIX     0x44    /* 固定地址写 */
#define TM_CMD_READ_KEY     0x42    /* 读按键 */
#define TM_CMD_ADDR_START   0xC0    /* 显示 RAM 起始地址 */
#define TM_CMD_DISP_OFF     0x80    /* 关显示 */
#define TM_CMD_DISP_ON      0x88    /* 开显示 + 亮度 0，|亮度 0~7 */

/* 模块上的 8 只 LED 一般挂在各自 GRID 的 SEG9，即"奇地址字节的 bit0"。
   如果你的模块点不亮 LED，把这里改成 0x02（SEG10）或先跑 [4] 逐个试。 */
#define TM_LED_BIT      0x01

/* 按键 2 行分别落在读回字节的哪一位。
   实测本模块：S1~S4 = bit0，S5~S8 = bit4（不是 bit1！）。
   换到别的模块若后四个键读不到，先用 [7] 的 extra 日志确认行位，再改这两行。 */
#define TM_KEY_ROW1     0x01
#define TM_KEY_ROW2     0x10

/* 常用段码 */
#define SEG_BLANK       0x00
#define SEG_MINUS       0x40
#define SEG_UNDERLINE   0x08

/* ---------------- 驱动 API ---------------- */
void TM_Init(void);
void TM_Flush(void);                            /* 影子缓冲整块下发 */
void TM_Clear(void);                            /* 清全部 16 字节 */
void TM_ClearDigits(void);                      /* 只清数码管，保留 LED */

void TM_SetBrightness(unsigned char level);     /* 0~7 */
void TM_DisplayOn(void);
void TM_DisplayOff(void);

void TM_SetDigit(unsigned char pos, unsigned char seg);   /* pos 0~7 */
void TM_SetDigits(unsigned char *segs);                   /* 8 字节一次刷完 */
void TM_SetHexDigit(unsigned char pos, unsigned char val);/* 0~F */
void TM_ShowHex(unsigned int value);                      /* 左对齐 4 位 */
void TM_ShowNumber(unsigned int value);                   /* 十进制右对齐 */
void TM_ShowString(unsigned char *s);                     /* 仅支持 0-9A-F 及符号 */

void TM_SetLEDs(unsigned char mask);            /* bit0~bit7 -> LED1~LED8 */
void TM_SetLED(unsigned char pos, bit on);
void TM_SetLedBytes(unsigned char dat);         /* 8 个奇数寄存器写同一字节，用于探 LED 位 */

unsigned char TM_ReadKeys(void);                /* bit0~bit7 = S1~S8，1=按下 */
void TM_ReadKeyBytes(unsigned char *buf);       /* 原始 4 字节，调试映射用 */
unsigned char TM_KeysFromBytes(unsigned char *buf); /* 4 字节 -> 8 位键值 */

/* ---------------- 测试 API（实现在 tm1638_test.c） ---------------- */
void TM_TestAll(void);
void TM_KeyMonitor(void);

#endif /* __TM1638_H__ */
