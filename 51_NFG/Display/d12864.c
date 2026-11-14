#include "d12864.h"
#if USE_ASCII_FONT
#include "ascii5x7.h"
#endif

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
/* 只有 USE_FONT_ROM = 1 时才编译这一整段(约 1.5KB), 否则 AT89C52 的
   8K Flash 装不下内置 ASCII 字库. 开关在 d12864.h 里. */
#if USE_FONT_ROM

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
        ROM_OUT = 1;            /* 准双向口: 写1置为输入态 */
        ROM_SCK = 0;            /* 下降沿后ROM输出下一位到SO */
        delay_us(5);            /* 等待SO数据稳定(STC8G主频高,必须延时) */
        ret_data <<= 1;
        if (ROM_OUT) ret_data |= 0x01;
        ROM_SCK = 1;
        delay_us(5);
    }
    return ret_data;
}

/* 从字库IC读出16x16点阵并显示 */
void get_and_write_16x16(ulong fontaddr, uchar page, uchar column)
{
    uchar i, j, disp_data;
    ROM_CS_LOW();
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
    ROM_CS_HIGH();
}

/* 从字库IC读出8x16点阵并显示 */
void get_and_write_8x16(ulong fontaddr, uchar page, uchar column)
{
    uchar i, j, disp_data;
    ROM_CS_LOW();
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
    ROM_CS_HIGH();
}

/* 从字库IC读出5x8点阵并显示 */
void get_and_write_5x8(ulong fontaddr, uchar page, uchar column)
{
    uchar i, disp_data;
    ROM_CS_LOW();
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
    ROM_CS_HIGH();
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
 *       引脚: ROM_IN=P2.7, ROM_OUT=P0.6, ROM_SCK=P0.5, ROM_CS 板上接地(宏为空)
 */
void rom_read_bytes(ulong addr, uchar *buf, uchar n)
{
    uchar i;
    ROM_CS_LOW();
    send_command_to_ROM(0x03);                   /* 读数据指令 */
    send_command_to_ROM((uchar)((addr >> 16) & 0xFF));  /* 地址高8位 */
    send_command_to_ROM((uchar)((addr >> 8)  & 0xFF));  /* 地址中8位 */
    send_command_to_ROM((uchar)( addr        & 0xFF));  /* 地址低8位 */
    for (i = 0; i < n; i++)
    {
        buf[i] = get_data_from_ROM();
    }
    ROM_CS_HIGH();
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

/*
 * 函数: rom_simple_read
 * 输入: addr - 字库ROM 24位地址
 * 输出: 读回的1字节数据
 * 说明: 诊断用. 从字库ROM指定地址读取单个字节并返回.
 *       若返回0xFF -> ROM_OUT未接或字库无响应;
 *       若返回0x00 -> ROM_OUT被拉低或数据确实为0;
 *       若返回其他值 -> 通讯正常.
 */
uchar rom_simple_read(ulong addr)
{
    uchar ret;
    ROM_CS_LOW();
    send_command_to_ROM(0x03);
    send_command_to_ROM((uchar)((addr >> 16) & 0xFF));
    send_command_to_ROM((uchar)((addr >> 8)  & 0xFF));
    send_command_to_ROM((uchar)( addr        & 0xFF));
    ret = get_data_from_ROM();
    ROM_CS_HIGH();
    return ret;
}

/* ============================================================
 *  自检 1: 整屏填充同一个字节 (不经过字库, 纯本地数据)
 * ============================================================ */
static void fill_screen(uchar v)
{
    uchar p, i;
    for (p = 1; p <= 8; p++)
    {
        lcd_address(p, 1);
        for (i = 0; i < 128; i++)
            transfer_data_lcd(v);
    }
}

/* ============================================================
 *  自检 0: level_window  —— 给万用表用的"静态电平窗口"
 *
 *  用万用表量一根正在翻转的时钟线, 读数是平均值(2.5~3.7V),
 *  完全没有意义. 这里造两个 3 秒的静止窗口:
 *
 *    前 3 秒 : P0.5 / P0.6 / P2.7 全部输出高, 整屏刷成全黑
 *              -> 此时量 P0.5, 有 10k 上拉 = 5.00V, 没上拉 = 0~4V 飘
 *    后 3 秒 : P0.5 输出低, 整屏刷成全白
 *              -> 此时量 P0.5 必须是 0.00V (证明能拉低、没开路)
 *
 *  屏的黑/白就是"当前是哪个窗口"的标志, 不用看表也能对时.
 * ============================================================ */
static void level_window(void)
{
    /* --- 高电平窗口 (整屏黑) --- */
    ROM_SCK = 1;
    ROM_IN  = 1;
    ROM_OUT = 1;            /* 写 1 = 置成输入态 */
    fill_screen(0xFF);
    delay(3000);

    /* --- 低电平窗口 (整屏白) --- */
    ROM_SCK = 0;
    fill_screen(0x00);
    delay(3000);

    ROM_SCK = 1;
    clear_screen();
}

/* ============================================================
 *  自检 2: draw_block
 *  画一个 16x16 的小方块: solid=1 实心, solid=0 空心(只有边框).
 *  用"实心/空心"表达 1/0, 正显负显都不会认错.
 * ============================================================ */
static void draw_block(uchar page, uchar col, uchar solid)
{
    uchar i, c, v;

    for (i = 0; i < 2; i++)          /* 方块占两个 page = 16 行 */
    {
        lcd_address(page + i, col);
        for (c = 0; c < 16; c++)
        {
            if (solid)                  v = 0xFF;          /* 实心 */
            else if (c == 0 || c == 15) v = 0xFF;          /* 左右边框 */
            else if (i == 0)            v = 0x01;          /* 上边框 */
            else                        v = 0x80;          /* 下边框 */
            transfer_data_lcd(v);
        }
    }
}

/* ============================================================
 *  自检 3: font_selftest
 *  返回 0 = 字库读通; 返回 1 = 有故障(屏上已画好 8 个状态方块, 并停住)
 *
 *  阶段 0 (3s + 3s) : 静态电平窗口, 屏黑时量 P0.5 应为 5.0V
 *  阶段 1 (1s)      : 整屏竖条纹, 证明 LCD 通路正常
 *  阶段 2           : 引脚回读 + 字库响应性, 结果画成 8 个方块
 *
 *  8 个方块从左到右 = bit7 .. bit0, 实心=1, 空心=0:
 *    第1块 bit7 : P0.5 写 1 后回读仍是 1   (能拉高, 上拉正常)
 *    第2块 bit6 : P0.5 写 0 后回读是 0      (能拉低, 线没断)
 *    第3块 bit5 : 读 addr0 不是全 0xFF      (SO 没被上拉钉死)
 *    第4块 bit4 : 读 addr0 不是全 0x00      (SO 没被拉死)
 *    第5块 bit3 : 同一地址连读两次一致      (时序稳定)
 *    第6块 bit2 : 换地址(0x3CF80)数据会变   (地址真的生效)
 *    第7块 bit1 : 数据里有非 00/FF 的字节   (确实读到了内容)
 *    第8块 bit0 : 控制位, 恒为 1            (为空说明屏/图案本身坏了)
 *
 *  正常: 8 块全是实心, 并且接着会显示"测试"和 FONT OK.
 * ============================================================ */
uchar font_selftest(void)
{
    uchar i, bits;
    uchar a[32], b[32];
    uchar sck_hi, sck_lo, not_ff, not_00, same, diff, content;
    uchar code s_ceshi[] = {0xB2, 0xE2, 0xCA, 0xD4, 0x00};          /* "测试" */
    uchar code s_ascii[] = {'F','O','N','T',' ','O','K',' ','1','2','3', 0x00};

    /* ---------- 阶段 0: 静态电平窗口 ---------- */
    level_window();

    /* ---------- 阶段 1: LCD 自检(纯本地数据) ---------- */
    fill_screen(0x55);
    delay(1000);
    clear_screen();

    /* ---------- 阶段 2a: 引脚回读 (8051 读端口读的是引脚真实电平) ---------- */
    ROM_SCK = 1;
    delay_us(200);
    sck_hi = (ROM_SCK != 0);

    ROM_SCK = 0;
    delay_us(200);
    sck_lo = (ROM_SCK == 0);

    ROM_SCK = 1;

    /* ---------- 阶段 2b: 字库响应性 ---------- */
    rom_read_bytes(0x000000UL, a, 32);      /* 字库起始处 */
    rom_read_bytes(0x000000UL, b, 32);      /* 同地址再读一次 */

    not_ff  = 0;
    not_00  = 0;
    same    = 1;
    content = 0;
    for (i = 0; i < 32; i++)
    {
        if (a[i] != 0xFF) not_ff = 1;
        if (a[i] != 0x00) not_00 = 1;
        if (a[i] != b[i]) same   = 0;
        if (a[i] != 0xFF && a[i] != 0x00) content = 1;
    }

    rom_read_bytes(0x3CF80UL, b, 32);       /* ASCII 字库区, 应和 addr0 不同 */
    diff = 0;
    for (i = 0; i < 32; i++)
    {
        if (a[i] != b[i]) diff = 1;
    }

    bits = 0x01;                            /* bit0 控制位 */
    if (sck_hi)  bits |= 0x80;
    if (sck_lo)  bits |= 0x40;
    if (not_ff)  bits |= 0x20;
    if (not_00)  bits |= 0x10;
    if (same)    bits |= 0x08;
    if (diff)    bits |= 0x04;
    if (content) bits |= 0x02;

    /* ---------- 全通: 直接显示真字 ---------- */
    if (bits == 0xFF)
    {
        display_GB2312_string(1, 1, s_ceshi);
        display_GB2312_string(3, 1, s_ascii);
        display_string_5x8  (5, 1, s_ascii);
        return 0;
    }

    /* ---------- 有故障: 画 8 个状态方块并停住 ---------- */
    clear_screen();
    for (i = 0; i < 8; i++)
    {
        draw_block(1, (uchar)(1 + i * 16), (uchar)((bits & (0x80 >> i)) ? 1 : 0));
    }
    return 1;
}

#endif  /* USE_FONT_ROM */

#if USE_ASCII_FONT
/* ============================================================
 *  内置 ASCII 字库显示 (不需要字库 IC, 只往 LCD 写点阵)
 *
 *  ascii5x7[] 里每个字符 5 字节, 列存, bit0 = 顶行, 与 LCD 显存
 *  的"一列一字节、bit0=页内第 0 行"完全一致, 所以可以原样搬过去,
 *  不需要任何位变换.
 * ============================================================ */

/*
 * 函数: lcd_text
 * 5x7 小字, 每字 5 列 + 1 列间距 = 6 列.
 * 128 列 -> 每行 21 个字符; page 1~8 -> 8 行, 一屏 168 字符.
 * 超出右边界自动停止, 不做折行.
 */
void lcd_text(uchar page, uchar col, uchar *s)
{
    uchar i, ch;
    uint  off;

    if (page < 1 || page > LCD_PAGES) return;
    if (col  < 1 || col  > LCD_WIDTH) return;

    while (*s)
    {
        ch = *s;
        s++;
        if (ch < 0x20 || ch > 0x7E) ch = '?';      /* 非可打印字符显示 ? */

        if (col > (LCD_WIDTH - 6 + 1)) break;      /* 这一行放不下了 */
        off = (uint)(ch - 0x20) * 5;

        lcd_address(page, col);
        for (i = 0; i < 5; i++)
            transfer_data_lcd(ascii5x7[off + i]);
        transfer_data_lcd(0x00);                   /* 1 列字间距 */

        col += 6;
    }
}

/*
 * 函数: lcd_text_2x
 * 把 5x7 字模按 2 倍放大成 10x14, 占 2 页, 每字占 11 列(含间距).
 * 128 列 -> 每行 11 个字符, 共 4 行. 适合做标题.
 * 放大方式: 源像素 (r,c) -> 目标 (2r,2c) 起的 2x2 像素块.
 */
void lcd_text_2x(uchar page, uchar col, uchar *s)
{
    uchar buf[20];                 /* 2 页 x 10 列 */
    uchar i, c, r, tr, tc, b, ch;
    uint  off;

    if (page < 1 || page > (LCD_PAGES - 1)) return;   /* 需要 2 页 */
    if (col  < 1 || col  > LCD_WIDTH)       return;

    while (*s)
    {
        ch = *s;
        s++;
        if (ch < 0x20 || ch > 0x7E) ch = '?';

        if (col > (LCD_WIDTH - 11 + 1)) break;
        off = (uint)(ch - 0x20) * 5;

        for (i = 0; i < 20; i++) buf[i] = 0x00;

        for (c = 0; c < 5; c++)
        {
            b = ascii5x7[off + c];
            for (r = 0; r < 7; r++)
            {
                if (b & (1 << r))
                {
                    tr = (uchar)(r << 1);
                    tc = (uchar)(c << 1);

                    buf[(tr >> 3) * 10 + tc]     |= (uchar)(1 << (tr & 7));
                    buf[(tr >> 3) * 10 + tc + 1] |= (uchar)(1 << (tr & 7));
                    tr++;
                    buf[(tr >> 3) * 10 + tc]     |= (uchar)(1 << (tr & 7));
                    buf[(tr >> 3) * 10 + tc + 1] |= (uchar)(1 << (tr & 7));
                }
            }
        }

        for (i = 0; i < 2; i++)
        {
            lcd_address(page + i, col);
            for (c = 0; c < 10; c++)
                transfer_data_lcd(buf[i * 10 + c]);
        }

        col += 11;
    }
}

#endif  /* USE_ASCII_FONT */
