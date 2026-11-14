#ifndef __D12864_H__
#define __D12864_H__

#include "stc90c58.h"

/* ================= 类型定义 ================= */
typedef unsigned char  uchar;
typedef unsigned int   uint;
typedef unsigned long  ulong;

/* ================= 功能开关 ================= */
/* 两种出字方式二选一 (AT89C52 只有 8K Flash, 同时开会溢出):
 *
 *   USE_FONT_ROM   1 = 走模块上的 25Q80 字库 IC, 可显示 16x16 GB2312 汉字
 *   USE_ASCII_FONT 1 = 用内置 5x7 ASCII 点阵, 不依赖任何外部字库
 *
 * 注意: USE_FONT_ROM 要求模块第 4 脚 ROM_CS 由 MCU 控制, 硬件见 ROM_CS_ON_MCU */
#define USE_FONT_ROM     0
#define USE_ASCII_FONT   1

#if (USE_FONT_ROM && USE_ASCII_FONT)
#error "8K Flash 装不下两套字库, USE_FONT_ROM / USE_ASCII_FONT 只能有一个 1"
#endif

/* ================= LCD 12864 引脚定义 ================= */
sbit LCD_CS   = P3^7;   /* 片选 */
sbit LCD_RST  = P3^6;   /* 复位 */
sbit LCD_AO   = P3^5;   /* 数据/命令选择 (RS/CD/A0) */
sbit LCD_LEDA = P3^4;   /* 背光 */
sbit LCD_SDA  = P2^6;   /* 串行数据 (SID) */
sbit LCD_SCL  = P2^5;   /* 串行时钟 (SCLK) */

/* ================= 字库 IC 引脚定义 ================= */
/* 注意: AT89C52/STC89C52 的 P0 是【开漏】口, 输出高电平全靠外部上拉.
 *       板上 R4 / R2 已经各给了 5.1k 上拉到 +5V, 这一项没问题. */
sbit ROM_IN   = P2^7;   /* 字库IC 数据输入 (SI) */
sbit ROM_OUT  = P0^6;   /* 字库IC 数据输出 (SO) */
sbit ROM_SCK  = P0^5;   /* 字库IC 时钟     (SCK) */

/* ---- 关键: 字库 IC 的片选 CS# ----
 * 现状 (PCB ver4.0): 模块第 4 脚 ROM_CS 在板上被【直接接地】, MCU 侧没有任何
 * 连线 (原理图上 P1.5 其实是 74HC165 的 DQ_SRCLK, 不能拿来当 CS).
 *
 * 这是读不出汉字的原因:
 *   25 系列 SPI Flash 每条命令都必须以 CS# 的一次"高->低"开始、以"高"结束.
 *   CS# 恒低时芯片永远停在"已选中 + 顺序读"状态, 内部地址计数器一直往前跑;
 *   上电后第一条 0x03 之后, 再发的 0x03 和 24 位地址全被当成时钟吃掉,
 *   读回来的是计数器后面的数据 -> 每次都不一样 -> 屏幕上的花纹和方块.
 *
 * 修好硬件后 (割断第4脚的地, 飞线到 P0.7, 并给 P0.7 加 10k 上拉),
 * 把下面改成 1 即可.  !!! 没割断地线前千万不要改 1 !!!
 * 否则 P0.7 输出高电平会顶死一个接地的节点, 可能烧 IO. */
#define ROM_CS_ON_MCU   0
#if ROM_CS_ON_MCU
sbit ROM_CS = P0^7;     /* 唯一的空闲脚, 32 脚, 开漏, 必须配上拉 */
#define ROM_CS_LOW()    (ROM_CS = 0)
#define ROM_CS_HIGH()   (ROM_CS = 1)
#else
#define ROM_CS_LOW()    do { } while (0)
#define ROM_CS_HIGH()   do { } while (0)
#endif

/* ================= 屏幕参数 ================= */
#define LCD_WIDTH    128    /* LCD 列数: 128 */
#define LCD_HEIGHT   64     /* LCD 行数: 64 */
#define LCD_PAGES    8      /* LCD 页数: 8 (每页8行) */
#define PAGE_HEIGHT  8      /* 每页高度: 8 像素 */

/* ================= 字库 ROM 基址常量 ================= */
#define GB16_OFFSET   846UL   /* GB2312 汉字区偏移(跳过前846个半角/符号) */
#define ASCII8_BASE   0x3CF80UL  /* 8x16 ASCII 字库基址 */
#define ASCII5_BASE   0x3BFC0UL  /* 5x7  ASCII 字库基址 */

/* ================= 函数声明 ================= */
void delay(uint n_ms);
void delay_us(uint n_us);

void transfer_command_lcd(uchar data1);
void transfer_data_lcd(uchar data1);
void initial_lcd(void);
void lcd_address(uchar page, uchar column);
void clear_screen(void);

void display_128x64(uchar *dp);
void display_graphic_16x16(uchar page, uchar column, uchar *dp);
void display_graphic_8x16(uchar page, uchar column, uchar *dp);
void display_graphic_5x8(uchar page, uchar column, uchar *dp);

void send_command_to_ROM(uchar datu);
uchar get_data_from_ROM(void);
void get_and_write_16x16(ulong fontaddr, uchar page, uchar column);
void get_and_write_8x16(ulong fontaddr, uchar page, uchar column);
void get_and_write_5x8(ulong fontaddr, uchar page, uchar column);

void display_GB2312_string(uchar page, uchar column, uchar *text);
void display_string_5x8(uchar page, uchar column, uchar *text);

/* ============ 新增: 字库通用读取 + 三种字号字符串 ============ */
void rom_read_bytes(ulong addr, uchar *buf, uchar n);
void show_str_16x16(uchar page, uchar col, uchar *buf);
void show_str_8x16 (uchar page, uchar col, uchar *buf);
void show_str_5x7  (uchar page, uchar col, uchar *buf);

/* ============ 字库ROM单字节读取(诊断用) ============ */
uchar rom_simple_read(ulong addr);

/* ============ 屏幕 + 字库 自检 ============ */
uchar font_selftest(void);   /* 0 = 字库正常; 1 = 有故障, 屏上已画好故障图案 */

/* ============ 内置 ASCII 字库显示 (不依赖字库 IC) ============ */
#if USE_ASCII_FONT
void lcd_text(uchar page, uchar col, uchar *s);      /* 5x7,  每字 6 列, 1 页高 */
void lcd_text_2x(uchar page, uchar col, uchar *s);   /* 2 倍放大 10x14, 每字 11 列, 2 页高 */
#endif

/* GB2312 区位 -> 16x16 点阵地址 (与晶联讯官方例程一致) */
#define GB16_ADDR(msb, lsb) \
    ((ulong)(((ulong)((msb) - 0xB0) * 94 + ((lsb) - 0xA1) + GB16_OFFSET) * 32))

/* ASCII 8x16 点阵地址 */
#define ASCII8_ADDR(ch) \
    ((ulong)(((ulong)((ch) - 0x20) * 16) + ASCII8_BASE))

#endif /* __D12864_H__ */