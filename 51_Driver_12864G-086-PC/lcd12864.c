#include "lcd12864.h"

/* ==========================================================================
 * JLX12864G-086 驱动实现
 * --------------------------------------------------------------------------
 * LCD 串行：SCK 空闲低，数据在上升沿被采样，MSB 先出；RS=1 数据 / 0 命令。
 * 字库串行：SPI 模式 0，命令 0x03 + 24 位地址（高字节先出），后面直接是数据。
 *
 * ★ 汉字点阵"边读边写"：一条 0x03 命令后连续读 32 字节，
 *   前 16 字节写 page，后 16 字节写 page+1 —— 不需要 32 字节缓冲，
 *   省下 RAM（STC89C52 只有 256B）。这也是 ROM_CS 必须整条命令包住的原因：
 *   中途拉高 CS 会打断顺序读，后 16 字节就取不到了。
 * ========================================================================== */

/* LCD 控制器初始化表（UC1701x 系列）
   黑屏/花屏时优先改这三处：
     A0 -> A1   左右镜像
     C8 -> C0   上下颠倒
     81 28      对比度（0x00~0x3F） */
static unsigned char code lcd_init_tab[] = {
    0xE2,       /* 软复位 */
    0x2C,       /* 升压步骤 1 */
    0x2E,       /* 升压步骤 2 */
    0x2F,       /* 升压步骤 3（稳压 + 跟随器） */
    0x23,       /* 粗调对比度 */
    0x81,       /* 微调对比度命令 */
    LCD_CONTRAST,
    0xA2,       /* 偏压 1/9 */
    0xC8,       /* 行扫描：上到下（颠倒用 0xC0） */
    0xA0,       /* 列扫描：左到右（镜像用 0xA1） */
    0x40,       /* 起始行 0 */
    0xAF        /* 开显示 */
};

static unsigned char s_dummy = FONT_DUMMY;

static void lcd_delay_long(void)
{
    unsigned char i, j;
    for (i = 0; i < 20; i++)
        for (j = 0; j < 200; j++)
            ;
}

/* ---------- LCD 串行底层 ---------- */

static void lcd_spi_byte(unsigned char dat, bit is_data)
{
    unsigned char i;

    LCD_RS = is_data;
    for (i = 0; i < 8; i++)
    {
        LCD_SCK = 0;
        if (dat & 0x80)
            LCD_SDA = 1;
        else
            LCD_SDA = 0;
        dat <<= 1;
        LCD_SCK = 1; /* 上升沿采样 */
    }
    LCD_SCK = 0;
}

void LCD_WriteCmd(unsigned char cmd)
{
    LCD_CS = 0;
    lcd_spi_byte(cmd, 0);
    LCD_CS = 1;
}

void LCD_WriteData(unsigned char dat)
{
    LCD_CS = 0;
    lcd_spi_byte(dat, 1);
    LCD_CS = 1;
}

void LCD_SetPos(unsigned char page, unsigned char col)
{
    LCD_WriteCmd((unsigned char)(0xB0 | (page & 0x07)));
    LCD_WriteCmd((unsigned char)(0x10 | ((col >> 4) & 0x0F)));
    LCD_WriteCmd((unsigned char)(col & 0x0F));
}

void LCD_Backlight(bit on)
{
    if (on)
        LCD_LEDA = 1;
    else
        LCD_LEDA = 0;
}

void LCD_Init(void)
{
    unsigned char i;

    LCD_CS = 1;
    LCD_SCK = 0;
    LCD_RS = 0;
    LCD_SDA = 1;

    /* 硬复位：参考例程给的是 100ms 量级，这里用长延时 */
    LCD_RST = 0;
    lcd_delay_long();
    LCD_RST = 1;
    lcd_delay_long();

    for (i = 0; i < sizeof(lcd_init_tab); i++)
    {
        LCD_WriteCmd(lcd_init_tab[i]);
        if (lcd_init_tab[i] == 0xE2 || lcd_init_tab[i] == 0x2C || lcd_init_tab[i] == 0x2E)
            lcd_delay_long(); /* 升压建立需要时间 */
    }
}

void LCD_Clear(void)
{
    unsigned char p;

    for (p = 0; p < LCD_PAGES; p++)
        LCD_FillPage(p, 0x00);
}

void LCD_FillPage(unsigned char page, unsigned char dat)
{
    unsigned char i;

    LCD_SetPos(page, 0);
    LCD_CS = 0;
    LCD_RS = 1;
    for (i = 0; i < LCD_WIDTH; i++)
    {
        LCD_SCK = 0;
        if (dat & 0x80)
            LCD_SDA = 1;
        else
            LCD_SDA = 0;
        dat = (unsigned char)((dat << 1) | (dat >> 7)); /* 循环移位：同一图案铺满一页 */
        LCD_SCK = 1;
    }
    LCD_SCK = 0;
    LCD_CS = 1;
}

/* ---------- 字库（25Q80 类 SPI Flash） ---------- */

void Font_SetDummy(unsigned char n)
{
    s_dummy = n;
}

unsigned char Font_Dummy(void)
{
    return s_dummy;
}

static void rom_send(unsigned char dat)
{
    unsigned char i;

    for (i = 0; i < 8; i++)
    {
        ROM_SCK = 0;
        if (dat & 0x80)
            ROM_IN = 1;
        else
            ROM_IN = 0;
        _nop_();
        _nop_();
        _nop_();
        dat <<= 1;
        ROM_SCK = 1;
        _nop_();
        _nop_();
        _nop_();
    }
    ROM_SCK = 0;
}

static unsigned char rom_read(void)
{
    unsigned char i, dat = 0;

    ROM_IN = 1;  /* 释放 MOSI，别和字库输出对顶 */
    ROM_OUT = 1; /* ★ 准双向口必须写 1 才能当输入，漏了会一直读到 0 */
    for (i = 0; i < 8; i++)
    {
        ROM_SCK = 0;
        _nop_();
        _nop_();
        dat <<= 1;
        if (ROM_OUT)
            dat |= 0x01;
        ROM_SCK = 1;
        _nop_();
        _nop_();
    }
    ROM_SCK = 0;
    return dat;
}

/* 发一条 0x03 读命令 + 24 位地址（CS 由调用者包住） */
static void rom_start_read(unsigned long addr)
{
    ROM_CS_LOW();
    rom_send(FONT_READ_CMD);
    rom_send((unsigned char)(addr >> 16));
    rom_send((unsigned char)(addr >> 8));
    rom_send((unsigned char)addr);
    ROM_OUT = 1;
}

/* GB2312 汉字 16x16 地址：((区-0xB0)*94 + (位-0xA1) + 846) * 32 */
unsigned long Font_AddrGB2312(unsigned char hi, unsigned char lo)
{
    unsigned int q;

    if (hi < 0xB0)
        hi = 0xB0;
    if (hi > 0xF7)
        hi = 0xF7;
    if (lo < 0xA1)
        lo = 0xA1;

    q = (unsigned int)(hi - 0xB0) * 94 + (unsigned int)(lo - 0xA1) + (unsigned int)GB16_OFFSET;
    return (unsigned long)q * 32;
}

/* ASCII 8x16 地址：(ch-0x20)*16 + 0x3CF80 */
unsigned long Font_AddrASCII(unsigned char ch)
{
    if (ch < 0x20)
        ch = 0x20;
    return (unsigned long)(ch - 0x20) * 16 + ASCII8_BASE;
}

/* 通用读取（诊断用）：连续读 len 字节到 buf */
bit Font_Read(unsigned long addr, unsigned char *buf, unsigned char len)
{
    unsigned char i;

    rom_start_read(addr);
    for (i = 0; i < s_dummy; i++)
        rom_read();
    for (i = 0; i < len; i++)
        buf[i] = rom_read();
    ROM_CS_HIGH();
    return 1;
}

/* ---------- 字符绘制（边读边写，不占缓冲） ---------- */

void LCD_PutASCII(unsigned char page, unsigned char col, unsigned char ch)
{
    unsigned char i;

    rom_start_read(Font_AddrASCII(ch));

    LCD_SetPos(page, col);
    LCD_CS = 0;
    LCD_RS = 1;
    for (i = 0; i < 8; i++)
        lcd_spi_byte(rom_read(), 1);
    LCD_CS = 1;

    LCD_SetPos((unsigned char)(page + 1), col);
    LCD_CS = 0;
    LCD_RS = 1;
    for (i = 0; i < 8; i++)
        lcd_spi_byte(rom_read(), 1);
    LCD_CS = 1;

    ROM_CS_HIGH();
}

void LCD_PutHanzi(unsigned char page, unsigned char col, unsigned char hi, unsigned char lo)
{
    unsigned char i;

    rom_start_read(Font_AddrGB2312(hi, lo));

    LCD_SetPos(page, col);
    LCD_CS = 0;
    LCD_RS = 1;
    for (i = 0; i < 16; i++)
        lcd_spi_byte(rom_read(), 1);
    LCD_CS = 1;

    LCD_SetPos((unsigned char)(page + 1), col);
    LCD_CS = 0;
    LCD_RS = 1;
    for (i = 0; i < 16; i++)
        lcd_spi_byte(rom_read(), 1);
    LCD_CS = 1;

    ROM_CS_HIGH();
}

void LCD_PutString(unsigned char page, unsigned char col, unsigned char *s)
{
    while (*s)
    {
        if (col > (LCD_WIDTH - 8))
            break;
        LCD_PutASCII(page, col, *s++);
        col = (unsigned char)(col + 8);
    }
}

void LCD_PutHanziString(unsigned char page, unsigned char col, unsigned char *gb)
{
    while (gb[0] && gb[1])
    {
        if (col > (LCD_WIDTH - 16))
            break;
        LCD_PutHanzi(page, col, gb[0], gb[1]);
        gb += 2;
        col = (unsigned char)(col + 16);
    }
}

void LCD_PutTile(unsigned char page, unsigned char col, unsigned char w, unsigned char code *tile)
{
    unsigned char i;

    LCD_SetPos(page, col);
    LCD_CS = 0;
    LCD_RS = 1;
    for (i = 0; i < w; i++)
        lcd_spi_byte(tile[2 * i], 1);
    LCD_CS = 1;

    LCD_SetPos((unsigned char)(page + 1), col);
    LCD_CS = 0;
    LCD_RS = 1;
    for (i = 0; i < w; i++)
        lcd_spi_byte(tile[2 * i + 1], 1);
    LCD_CS = 1;
}
