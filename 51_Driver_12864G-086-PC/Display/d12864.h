#ifndef __D12864_H__
#define __D12864_H__

#include "stc90c52.h"

/* ================= 类型定义 ================= */
typedef unsigned char  uchar;
typedef unsigned int   uint;
typedef unsigned long  ulong;

/* ================= LCD 12864 引脚定义 ================= */
sbit LCD_CS   = P3^7;   /* 片选 */
sbit LCD_RST  = P3^6;   /* 复位 */
sbit LCD_AO   = P3^5;   /* 数据/命令选择 (RS/CD/A0) */
sbit LCD_LEDA = P3^4;   /* 背光 */
sbit LCD_SDA  = P2^6;   /* 串行数据 (SID) */
sbit LCD_SCL  = P2^5;   /* 串行时钟 (SCLK) */

/* ================= 字库 IC 引脚定义 ================= */
sbit ROM_IN   = P1^2;   /* 字库IC 数据输入 (SI) */
sbit ROM_OUT  = P1^3;   /* 字库IC 数据输出 (SO) */
sbit ROM_SCK  = P1^4;   /* 字库IC 时钟 (SCK) */
sbit ROM_CS   = P1^5;   /* 字库IC 片选 (CS#) */

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

#endif /* __D12864_H__ */