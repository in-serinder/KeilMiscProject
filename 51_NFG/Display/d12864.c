#include "d12864.h"

/* ================= 延时函数 ================= */
void delay(uint n_ms)
{
    uint j, k;
    for (j = 0; j < n_ms; j++)
        for (k = 0; k < 110; k++);
}

void delay_us(uint n_us)
{
    uint j, k;
    for (j = 0; j < n_us; j++)
        for (k = 0; k < 1; k++);
}

/* ================= LCD 底层串行传输 ================= */
/* 写指令到LCD模块 */
void transfer_command_lcd(uchar data1)
{
    char i;
    LCD_CS = 0;
    LCD_AO = 0;           /* 指令模式 */
    for (i = 0; i < 8; i++)
    {
        LCD_SCL = 0;
        if (data1 & 0x80) LCD_SDA = 1;
        else              LCD_SDA = 0;
        LCD_SCL = 1;
        data1 <<= 1;
    }
    LCD_CS = 1;
}

/* 写数据到LCD模块 */
void transfer_data_lcd(uchar data1)
{
    char i;
    LCD_CS = 0;
    LCD_AO = 1;           /* 数据模式 */
    for (i = 0; i < 8; i++)
    {
        LCD_SCL = 0;
        if (data1 & 0x80) LCD_SDA = 1;
        else              LCD_SDA = 0;
        LCD_SCL = 1;
        data1 <<= 1;
    }
    LCD_CS = 1;
}

/* ================= LCD 初始化 ================= */
void initial_lcd(void)
{
    LCD_RST = 0;          /* 低电平复位 */
    delay(100);
    LCD_RST = 1;          /* 复位完毕 */
    delay(100);

    transfer_command_lcd(0xE2);  /* 软复位 */
    delay(5);
    transfer_command_lcd(0x2C);  /* 升压步骤 1 */
    delay(50);
    transfer_command_lcd(0x2E);  /* 升压步骤 2 */
    delay(50);
    transfer_command_lcd(0x2F);  /* 升压步骤 3 */
    delay(5);
    transfer_command_lcd(0x23);  /* 粗调对比度 0x20~0x27 */
    transfer_command_lcd(0x81);  /* 微调对比度 */
    transfer_command_lcd(0x28);  /* 微调对比度值 0x00~0x3F */
    transfer_command_lcd(0xA2);  /* 1/9 偏压比 */
    transfer_command_lcd(0xC8);  /* 行扫描: 从上到下 */
    transfer_command_lcd(0xA0);  /* 列扫描: 从左到右 */
    transfer_command_lcd(0x40);  /* 起始行: 第一行 */
    transfer_command_lcd(0xAF);  /* 开显示 */
}

/* 设置页地址与列地址 */
void lcd_address(uchar page, uchar column)
{
    column = column - 0x01;
    transfer_command_lcd(0xB0 + page - 1);              /* 页地址 */
    transfer_command_lcd(0x10 + (column >> 4 & 0x0F));  /* 列地址高4位 */
    transfer_command_lcd(column & 0x0F);                /* 列地址低4位 */
}

/* 全屏清屏 */
void clear_screen(void)
{
    uchar i, j;
    for (i = 0; i < 9; i++)
    {
        transfer_command_lcd(0xB0 + i);
        transfer_command_lcd(0x10);
        transfer_command_lcd(0x00);
        for (j = 0; j < 132; j++)
            transfer_data_lcd(0x00);
    }
}

/* ================= 图形显示 ================= */
/* 128x64 整屏图像 */
void display_128x64(uchar *dp)
{
    uint i, j;
    for (j = 0; j < 8; j++)
    {
        lcd_address(j + 1, 1);
        for (i = 0; i < 128; i++)
        {
            transfer_data_lcd(*dp);
            dp++;
        }
    }
}

/* 16x16 点阵(汉字/图标) */
void display_graphic_16x16(uchar page, uchar column, uchar *dp)
{
    uint i, j;
    for (j = 0; j < 2; j++)
    {
        lcd_address(page + j, column);
        for (i = 0; i < 16; i++)
        {
            transfer_data_lcd(*dp);
            dp++;
        }
    }
}

/* 8x16 点阵(ASCII/图标) */
void display_graphic_8x16(uchar page, uchar column, uchar *dp)
{
    uint i, j;
    for (j = 0; j < 2; j++)
    {
        lcd_address(page + j, column);
        for (i = 0; i < 8; i++)
        {
            transfer_data_lcd(*dp);
            dp++;
        }
    }
}

/* 5x8 点阵(ASCII/图标) */
void display_graphic_5x8(uchar page, uchar column, uchar *dp)
{
    uint i;
    lcd_address(page, column);
    for (i = 0; i < 6; i++)
    {
        transfer_data_lcd(*dp);
        dp++;
    }
}

/* ================= 字库 IC 操作 ================= */
/* 送指令到字库IC */
void send_command_to_ROM(uchar datu)
{
    uchar i;
    for (i = 0; i < 8; i++)
    {
        ROM_SCK = 0;
        delay_us(10);
        if (datu & 0x80) ROM_IN = 1;
        else             ROM_IN = 0;
        datu <<= 1;
        ROM_SCK = 1;
        delay_us(10);
    }
}

/* 从字库IC读取1字节 */
uchar get_data_from_ROM(void)
{
    uchar i;
    uchar ret_data = 0;
    for (i = 0; i < 8; i++)
    {
        ROM_OUT = 1;
        ROM_SCK = 0;
        ret_data <<= 1;
        if (ROM_OUT) ret_data |= 0x01;
        ROM_SCK = 1;
    }
    return ret_data;
}

/* 从字库IC读出16x16点阵并显示 */
void get_and_write_16x16(ulong fontaddr, uchar page, uchar column)
{
    uchar i, j, disp_data;
    ROM_CS = 0;
    send_command_to_ROM(0x03);
    send_command_to_ROM((fontaddr & 0xFF0000) >> 16);
    send_command_to_ROM((fontaddr & 0xFF00) >> 8);
    send_command_to_ROM(fontaddr & 0xFF);
    for (j = 0; j < 2; j++)
    {
        lcd_address(page + j, column);
        for (i = 0; i < 16; i++)
        {
            disp_data = get_data_from_ROM();
            transfer_data_lcd(disp_data);
        }
    }
    ROM_CS = 1;
}

/* 从字库IC读出8x16点阵并显示 */
void get_and_write_8x16(ulong fontaddr, uchar page, uchar column)
{
    uchar i, j, disp_data;
    ROM_CS = 0;
    send_command_to_ROM(0x03);
    send_command_to_ROM((fontaddr & 0xFF0000) >> 16);
    send_command_to_ROM((fontaddr & 0xFF00) >> 8);
    send_command_to_ROM(fontaddr & 0xFF);
    for (j = 0; j < 2; j++)
    {
        lcd_address(page + j, column);
        for (i = 0; i < 8; i++)
        {
            disp_data = get_data_from_ROM();
            transfer_data_lcd(disp_data);
        }
    }
    ROM_CS = 1;
}

/* 从字库IC读出5x8点阵并显示 */
void get_and_write_5x8(ulong fontaddr, uchar page, uchar column)
{
    uchar i, disp_data;
    ROM_CS = 0;
    send_command_to_ROM(0x03);
    send_command_to_ROM((fontaddr & 0xFF0000) >> 16);
    send_command_to_ROM((fontaddr & 0xFF00) >> 8);
    send_command_to_ROM(fontaddr & 0xFF);
    lcd_address(page, column);
    for (i = 0; i < 5; i++)
    {
        disp_data = get_data_from_ROM();
        transfer_data_lcd(disp_data);
    }
    ROM_CS = 1;
}

/* ================= 字符串显示 ================= */
/* 显示GB2312字符串(16x16汉字 / 8x16 ASCII) */
void display_GB2312_string(uchar page, uchar column, uchar *text)
{
    uchar i = 0;
    ulong fontaddr;
    while (text[i] > 0x00)
    {
        if (((text[i] >= 0xB0) && (text[i] <= 0xF7)) && (text[i + 1] >= 0xA1))
        {
            /* GB2312 简体汉字地址: ((MSB-0xB0)*94 + (LSB-0xA1) + 846) * 32 */
            fontaddr  = (text[i] - 0xB0) * 94;
            fontaddr += (text[i + 1] - 0xA1) + 846;
            fontaddr  = (ulong)(fontaddr * 32);
            get_and_write_16x16(fontaddr, page, column);
            i += 2;
            column += 16;
        }
        else if (((text[i] >= 0xA1) && (text[i] <= 0xA3)) && (text[i + 1] >= 0xA1))
        {
            /* GB2312 15x16 字符地址: ((MSB-0xA1)*94 + (LSB-0xA1)) * 32 */
            fontaddr  = (text[i] - 0xA1) * 94;
            fontaddr += (text[i + 1] - 0xA1);
            fontaddr  = (ulong)(fontaddr * 32);
            get_and_write_16x16(fontaddr, page, column);
            i += 2;
            column += 16;
        }
        else if ((text[i] >= 0x20) && (text[i] <= 0x7E))
        {
            /* 8x16 ASCII 基址 0x3CF80 */
            fontaddr  = (text[i] - 0x20);
            fontaddr  = (ulong)(fontaddr * 16);
            fontaddr  = (ulong)(fontaddr + 0x3CF80);
            get_and_write_8x16(fontaddr, page, column);
            i += 1;
            column += 8;
        }
        else
        {
            i++;
        }
    }
}

/* 显示5x8 ASCII字符串 */
void display_string_5x8(uchar page, uchar column, uchar *text)
{
    uchar i = 0;
    ulong fontaddr;
    while (text[i] > 0x00)
    {
        if ((text[i] >= 0x20) && (text[i] <= 0x7E))
        {
            /* 5x8 ASCII 基址 0x3BFC0 */
            fontaddr  = (text[i] - 0x20);
            fontaddr  = (ulong)(fontaddr * 8);
            fontaddr  = (ulong)(fontaddr + 0x3BFC0);
            get_and_write_5x8(fontaddr, page, column);
            i += 1;
            column += 6;
        }
        else
        {
            i++;
        }
    }
}

/* ============================================================
 *  新增函数: 字库通用读取 + 三种字号字符串显示
 *  自查要点:
 *   - fontaddr 使用 unsigned long, 防止8位变量溢出
 *   - 16点阵汉字写连续2个page(page, page+1), 避免只显示半个
 *   - LCD SPI 引脚 (LCD_SCL/LCD_SDA) 与 字库ROM SPI 引脚
 *     (ROM_SCK/ROM_IN/ROM_OUT) 完全独立, 不共用
 *   - 字库读取使用指令 0x03, 24位地址顺序: 高8 -> 中8 -> 低8
 *   - 点阵竖置横排, 读取后直接送LCD, 不翻转字节
 *   - 边界判断: page 1~8, col 1~128, 超出则跳过该字符
 * ============================================================ */

/*
 * 函数: rom_read_bytes
 * 输入: addr  - 字库ROM 24位起始地址 (unsigned long)
 *       buf   - 数据存放缓冲区指针
 *       n     - 读取字节数 (1~255)
 * 输出: 无 (数据写入 buf)
 * 说明: 从字库ROM指定24bit地址连续读取n个字节.
 *       使用SPI读指令 0x03, 地址发送顺序: 高8位->中8位->低8位.
 *       引脚: ROM_CS(P1.5), ROM_SCK(P1.4), ROM_IN(P1.2), ROM_OUT(P1.3)
 */
void rom_read_bytes(ulong addr, uchar *buf, uchar n)
{
    uchar i;
    ROM_CS = 0;                                  /* 片选选中字库IC */
    send_command_to_ROM(0x03);                   /* 读数据指令 */
    send_command_to_ROM((uchar)((addr >> 16) & 0xFF));  /* 地址高8位 */
    send_command_to_ROM((uchar)((addr >> 8)  & 0xFF));  /* 地址中8位 */
    send_command_to_ROM((uchar)( addr        & 0xFF));  /* 地址低8位 */
    for (i = 0; i < n; i++)
    {
        buf[i] = get_data_from_ROM();
    }
    ROM_CS = 1;                                  /* 释放片选 */
}

/*
 * 函数: show_str_16x16
 * 输入: page - 起始页 (1~8, 因为汉字占2页, page最大为7)
 *       col  - 起始列 (1~128)
 *       buf  - 字符串指针 (GB2312编码, 含\0结束)
 * 输出: 无
 * 说明: 显示15x16大号GB2312汉字 (占16列宽, 2个page).
 *       遇到ASCII字符(0x20~0x7E)自动切换为8x16点阵 (占8列宽).
 *       边界: page+1<=8 且 col+宽度<=128 才显示, 否则跳过.
 */
void show_str_16x16(uchar page, uchar col, uchar *buf)
{
    uchar i = 0;
    ulong fontaddr;
    uchar  tmp[32];    /* 最大16x16=32字节 */
    uchar  j, k;

    /* 起始页越界: 汉字需要2页, 所以 page 必须 <= 7 */
    if (page < 1 || page > (LCD_PAGES - 1)) return;
    if (col  < 1 || col  > LCD_WIDTH)       return;

    while (buf[i] != 0)
    {
        /* ---- GB2312 汉字 (16x16) ---- */
        if ((buf[i] >= 0xB0 && buf[i] <= 0xF7) && (buf[i+1] >= 0xA1))
        {
            /* 边界: 剩余列数 >= 16 才显示 */
            if (col > (LCD_WIDTH - 16 + 1)) break;
            /* 地址计算: ((MSB-0xB0)*94 + (LSB-0xA1) + 846) * 32 */
            fontaddr  = (ulong)(buf[i]   - 0xB0) * 94;
            fontaddr += (ulong)(buf[i+1] - 0xA1) + GB16_OFFSET;
            fontaddr *= 32;
            rom_read_bytes(fontaddr, tmp, 32);      /* 读32字节点阵 */
            /* 写2个连续page: page 和 page+1 */
            for (j = 0; j < 2; j++)
            {
                lcd_address(page + j, col);
                for (k = 0; k < 16; k++)
                {
                    transfer_data_lcd(tmp[j * 16 + k]);  /* 竖置横排, 直接发送 */
                }
            }
            col += 16;
            i   += 2;
        }
        /* ---- GB2312 符号区 A1~A3 (16x16) ---- */
        else if ((buf[i] >= 0xA1 && buf[i] <= 0xA3) && (buf[i+1] >= 0xA1))
        {
            if (col > (LCD_WIDTH - 16 + 1)) break;
            fontaddr  = (ulong)(buf[i]   - 0xA1) * 94;
            fontaddr += (ulong)(buf[i+1] - 0xA1);
            fontaddr *= 32;
            rom_read_bytes(fontaddr, tmp, 32);
            for (j = 0; j < 2; j++)
            {
                lcd_address(page + j, col);
                for (k = 0; k < 16; k++)
                    transfer_data_lcd(tmp[j * 16 + k]);
            }
            col += 16;
            i   += 2;
        }
        /* ---- ASCII 自动切换 8x16 ---- */
        else if (buf[i] >= 0x20 && buf[i] <= 0x7E)
        {
            if (col > (LCD_WIDTH - 8 + 1)) break;
            fontaddr  = (ulong)(buf[i] - 0x20) * 16;
            fontaddr += ASCII8_BASE;
            rom_read_bytes(fontaddr, tmp, 16);      /* 读16字节点阵 */
            for (j = 0; j < 2; j++)
            {
                lcd_address(page + j, col);
                for (k = 0; k < 8; k++)
                    transfer_data_lcd(tmp[j * 8 + k]);
            }
            col += 8;
            i   += 1;
        }
        else
        {
            i++;   /* 非法字符跳过 */
        }
    }
}

/*
 * 函数: show_str_8x16
 * 输入: page - 起始页 (1~7, 字符高16像素占2页)
 *       col  - 起始列 (1~128)
 *       buf  - 字符串指针 (GB2312编码)
 * 输出: 无
 * 说明: 中号字符显示. 汉字16列宽(16x16), 英文8列宽(8x16).
 *       高度均为16像素(2个page).
 *       边界保护同 show_str_16x16.
 */
void show_str_8x16(uchar page, uchar col, uchar *buf)
{
    /* 8x16 中号与 16x16 大号使用相同的字库与尺寸,
       汉字16x16占16列, ASCII 8x16占8列, 高度均为2页 */
    show_str_16x16(page, col, buf);
}

/*
 * 函数: show_str_5x7
 * 输入: page - 页号 (1~8, 5x7字符高8像素占1页)
 *       col  - 起始列 (1~128)
 *       buf  - 字符串指针 (仅ASCII 0x20~0x7E有效)
 * 输出: 无
 * 说明: 小号ASCII字符显示, 每个字符宽5列+1列间距=6列,
 *       高8像素占1个page. 非ASCII字符自动跳过.
 *       边界: col+6 <= 128 才显示.
 */
void show_str_5x7(uchar page, uchar col, uchar *buf)
{
    uchar i = 0;
    ulong fontaddr;
    uchar  tmp[8];    /* 5x7 字符占5字节, 留余量 */
    uchar  k;

    /* 边界检查 */
    if (page < 1 || page > LCD_PAGES) return;
    if (col  < 1 || col  > LCD_WIDTH) return;

    while (buf[i] != 0)
    {
        if (buf[i] >= 0x20 && buf[i] <= 0x7E)
        {
            /* 边界: 剩余列数 >= 6 (5数据列+1间距) 才显示 */
            if (col > (LCD_WIDTH - 6 + 1)) break;
            /* 地址计算: (char - 0x20) * 8 + ASCII5_BASE */
            fontaddr  = (ulong)(buf[i] - 0x20) * 8;
            fontaddr += ASCII5_BASE;
            rom_read_bytes(fontaddr, tmp, 5);       /* 读5字节点阵 */
            lcd_address(page, col);
            for (k = 0; k < 5; k++)
                transfer_data_lcd(tmp[k]);          /* 竖置横排直接发送 */
            transfer_data_lcd(0x00);                /* 1列间距 */
            col += 6;
            i   += 1;
        }
        else
        {
            i++;   /* 非ASCII跳过 */
        }
    }
}