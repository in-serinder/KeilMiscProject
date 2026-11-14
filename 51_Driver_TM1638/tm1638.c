#include "tm1638.h"

/* ==========================================================================
 * TM1638 驱动实现（三线串行，纯 GPIO 位操作）
 * --------------------------------------------------------------------------
 * 时序：CLK 空闲为高；DIO 在 CLK 低电平期间改变，在 CLK 上升沿被模块采样。
 *       STB 下降沿开始一帧，STB 上升沿结束并生效。
 * 速度：11.0592MHz 下两个 _nop_() 约 2.2us，时钟约 450kHz，
 *       TM1638 手册上限 1MHz、最窄脉宽 100ns，这个速度既安全又好排查。
 *========================================================================== */

/* 显示 RAM 影子：16 字节，所有设置先改这里，再整块下发，
   这样单独改一位数码管不会把 LED 和其他位冲掉。 */
static unsigned char s_ram[TM_REG_BYTES];

/* 0~9 A~F 段码，bit0=a … bit6=g，bit7=dp */
static unsigned char code s_font[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};

static void TM_Delay(void)
{
    _nop_();
    _nop_();
}

/* ---------- 位操作原语 ---------- */

static void TM_Start(void)
{
    TM_CLK = 1;
    TM_DIO = 1;
    TM_Delay();
    TM_STB = 0; /* 下降沿：一帧开始 */
    TM_Delay();
}

static void TM_Stop(void)
{
    TM_CLK = 0;
    TM_Delay();
    TM_STB = 1; /* 上升沿：本帧生效 */
    TM_Delay();
    TM_CLK = 1;
    TM_DIO = 1;
}

/* LSB 在前发送一个字节 */
static void TM_SendByte(unsigned char dat)
{
    unsigned char i;

    for (i = 0; i < 8; i++)
    {
        TM_CLK = 0;
        if (dat & 0x01)
            TM_DIO = 1;
        else
            TM_DIO = 0;
        TM_Delay();
        TM_CLK = 1; /* 上升沿锁存 */
        TM_Delay();
        dat >>= 1;
    }
    TM_CLK = 0;
}

/* LSB 在前接收一个字节 */
static unsigned char TM_ReadByte(void)
{
    unsigned char i, dat = 0;

    TM_DIO = 1; /* 释放总线，交给模块驱动 */
    for (i = 0; i < 8; i++)
    {
        dat >>= 1;
        TM_CLK = 1;
        TM_Delay();
        if (TM_DIO)
            dat |= 0x80;
        TM_CLK = 0;
        TM_Delay();
    }
    return dat;
}

static void TM_WriteCmd(unsigned char cmd)
{
    TM_Start();
    TM_SendByte(cmd);
    TM_Stop();
}

/* ---------- 对外 API ---------- */

void TM_Init(void)
{
    unsigned char i;

    TM_STB = 1;
    TM_CLK = 1;
    TM_DIO = 1;

    for (i = 0; i < TM_REG_BYTES; i++)
        s_ram[i] = 0x00;

    TM_Flush();
    TM_WriteCmd(TM_CMD_DISP_ON | 0x07);
}

void TM_Flush(void)
{
    unsigned char i;

    TM_WriteCmd(TM_CMD_DATA_AUTO); /* 自动地址递增 */

    TM_Start();
    TM_SendByte(TM_CMD_ADDR_START);
    for (i = 0; i < TM_REG_BYTES; i++)
        TM_SendByte(s_ram[i]);
    TM_Stop();
}

void TM_Clear(void)
{
    unsigned char i;
    for (i = 0; i < TM_REG_BYTES; i++)
        s_ram[i] = 0x00;
    TM_Flush();
}

void TM_ClearDigits(void)
{
    unsigned char i;
    for (i = 0; i < TM_DIGITS; i++)
        s_ram[2 * i] = SEG_BLANK;
    TM_Flush();
}

void TM_SetBrightness(unsigned char level)
{
    if (level > 7)
        level = 7;
    TM_WriteCmd(TM_CMD_DISP_ON | level);
}

void TM_DisplayOn(void)
{
    TM_WriteCmd(TM_CMD_DISP_ON | 0x07);
}

void TM_DisplayOff(void)
{
    TM_WriteCmd(TM_CMD_DISP_OFF);
}

void TM_SetDigit(unsigned char pos, unsigned char seg)
{
    if (pos >= TM_DIGITS)
        return;
    s_ram[2 * pos] = seg;
    TM_Flush();
}

void TM_SetDigits(unsigned char *segs)
{
    unsigned char i;
    for (i = 0; i < TM_DIGITS; i++)
        s_ram[2 * i] = segs[i];
    TM_Flush();
}

void TM_SetHexDigit(unsigned char pos, unsigned char val)
{
    if (pos >= TM_DIGITS)
        return;
    s_ram[2 * pos] = s_font[val & 0x0F];
    TM_Flush();
}

void TM_ShowHex(unsigned int value)
{
    s_ram[0] = s_font[(unsigned char)(value >> 12) & 0x0F];
    s_ram[2] = s_font[(unsigned char)(value >> 8) & 0x0F];
    s_ram[4] = s_font[(unsigned char)(value >> 4) & 0x0F];
    s_ram[6] = s_font[(unsigned char)value & 0x0F];
    TM_Flush();
}

void TM_ShowNumber(unsigned int value)
{
    unsigned char i;

    for (i = 0; i < TM_DIGITS; i++)
        s_ram[2 * i] = SEG_BLANK;

    i = TM_DIGITS - 1;
    while (1)
    {
        s_ram[2 * i] = s_font[value % 10];
        value /= 10;
        if (i == 0 || value == 0)
            break;
        i--;
    }
    TM_Flush();
}

/* 只认 0-9 A-F 和 '-' '_' ' '，其余按空白处理 */
void TM_ShowString(unsigned char *s)
{
    unsigned char i, c;

    for (i = 0; i < TM_DIGITS; i++)
        s_ram[2 * i] = SEG_BLANK;

    i = 0;
    while (i < TM_DIGITS && *s)
    {
        c = *s++;
        if (c >= '0' && c <= '9')
            s_ram[2 * i] = s_font[c - '0'];
        else if (c >= 'A' && c <= 'F')
            s_ram[2 * i] = s_font[c - 'A' + 10];
        else if (c >= 'a' && c <= 'f')
            s_ram[2 * i] = s_font[c - 'a' + 10];
        else if (c == '-')
            s_ram[2 * i] = SEG_MINUS;
        else if (c == '_')
            s_ram[2 * i] = SEG_UNDERLINE;
        else
            s_ram[2 * i] = SEG_BLANK;
        i++;
    }
    TM_Flush();
}

void TM_SetLEDs(unsigned char mask)
{
    unsigned char i;
    for (i = 0; i < TM_DIGITS; i++)
    {
        if (mask & (unsigned char)(1 << i))
            s_ram[2 * i + 1] |= TM_LED_BIT;
        else
            s_ram[2 * i + 1] &= (unsigned char)~TM_LED_BIT;
    }
    TM_Flush();
}

/* 把 8 个奇数寄存器（0xC1/0xC3/…/0xCF）整字节写成同一个值。
   用于探测模块的 LED 到底挂在 SEG9(bit0) 还是 SEG10(bit1)：
   依次写 0x01 / 0x02 / 0x03，看哪一次 LED 亮。 */
void TM_SetLedBytes(unsigned char dat)
{
    unsigned char i;
    for (i = 0; i < TM_DIGITS; i++)
        s_ram[2 * i + 1] = dat;
    TM_Flush();
}

void TM_SetLED(unsigned char pos, bit on)
{
    if (pos >= TM_DIGITS)
        return;
    if (on)
        s_ram[2 * pos + 1] |= TM_LED_BIT;
    else
        s_ram[2 * pos + 1] &= (unsigned char)~TM_LED_BIT;
    TM_Flush();
}

/* 读回 4 个原始字节（不做任何位映射），调试按键矩阵时用它 */
void TM_ReadKeyBytes(unsigned char *buf)
{
    unsigned char i;

    TM_Start();
    TM_SendByte(TM_CMD_READ_KEY);
    TM_DIO = 1; /* 交还总线 */

    for (i = 0; i < 4; i++)
        buf[i] = TM_ReadByte();

    TM_Stop();
}

/* 4 字节原始数据 -> 8 位键值。
   字节 i = 第 i+1 列；两行分别取 TM_KEY_ROW1 / TM_KEY_ROW2（本模块是 bit0 与 bit4）。
   注意两个坑：
     - 不能按"顺序映射"，否则按 S1/S2/S3/S4 会错报成 S1/S3/S5/S7；
     - 第二行不是 bit1，按 bit1 解会让 S5~S8 完全读不到。 */
unsigned char TM_KeysFromBytes(unsigned char *buf)
{
    unsigned char i, key = 0;

    for (i = 0; i < 4; i++)
    {
        if (buf[i] & TM_KEY_ROW1)
            key |= (unsigned char)(1 << i);
        if (buf[i] & TM_KEY_ROW2)
            key |= (unsigned char)(1 << (i + 4));
    }
    return key;
}

unsigned char TM_ReadKeys(void)
{
    unsigned char buf[4];

    TM_ReadKeyBytes(buf);
    return TM_KeysFromBytes(buf);
}
