#ifndef __LCD12864_H__
#define __LCD12864_H__

#include "stc89c52.c"

/* ==========================================================================
 * JLX12864G-086-PC / -PN   128x64 单色 COG 液晶 + 外挂 GB2312 字库芯片
 * --------------------------------------------------------------------------
 * 模块上有**两套独立的串行总线**，别当成一套：
 *
 *   LCD 控制器（UC1701x 类）：  SCK / SDA / CS / RS(A0) / RST
 *   GB2312 字库（25Q80 类 SPI Flash）：ROM_SCK / ROM_IN(MOSI) / ROM_OUT(MISO) / ROM_CS
 *
 * 接线（按你给的）：
 *   P10  RS      A0：1 = 数据，0 = 命令
 *   P11  RST     复位，低有效
 *   P12  CS      LCD 片选，低有效
 *   P25  SCK     LCD 时钟
 *   P26  SDA     LCD 数据（只写，这颗控制器串行模式不支持回读）
 *   P24  LEDA    背光
 *   P23  ROM_CS  字库片选  ← 见下面 ROM_CS_ON_MCU 的安全开关
 *   P22  ROM_SCK 字库时钟
 *   P20  ROM_IN  字库数据输入（MCU -> 字库，MOSI）
 *   P21  ROM_OUT 字库数据输出（字库 -> MCU，MISO）
 *
 * --------------------------------------------------------------------------
 * 设计约束：**不做帧缓存**。128x64 = 1024 字节，STC89C52 只有 256B 片内 RAM；
 * 且 UC1701x 串行模式单向写入、不能回读，画点需要的"读-改-写"也做不了。
 * 所以本驱动是字符级直写：ASCII 8x16、汉字 16x16、整页填充、整块贴图。
 * ========================================================================== */

/* ---------------- LCD 引脚 ---------------- */
#define LCD_RS          P10
#define LCD_RST         P11
#define LCD_CS          P12
#define LCD_SCK         P25
#define LCD_SDA         P26
#define LCD_LEDA        P24

/* ---------------- 字库引脚 ---------------- */
#define ROM_SCK         P22
#define ROM_IN          P20     /* 丝印 ROM_IN：接字库数据输入（MOSI） */
#define ROM_OUT         P21     /* 丝印 ROM_OUT：字库输出（MISO） */

/* 若读回来全是 FF，先试着把这两个宏对调：
   #define ROM_IN   P21
   #define ROM_OUT  P20  */

/* ★ 安全开关：部分版本的模块把 ROM_CS 在板上**直接接地**，MCU 侧没引出来。
   那种情况下 P23 输出高电平会顶死一个接地节点，可能烧 IO！
   上电前先用万用表量模块 ROM_CS 脚对地是否导通：
     不导通 -> 保持 1（正常由 P23 控制）
     导通   -> 立刻改成 0，并且不要给 P23 输出高；字库暂时读不了，
               必须割断地线、飞线出来才能用。
   为什么要 CS：25 系列 SPI Flash 每条 0x03 命令必须以 CS 的下跳开始。
   CS 恒低时芯片永远停在"顺序读"状态，内部地址计数器一直往前跑，
   后面发的 0x03 和 24 位地址会被当成时钟吃掉，读回的是错位数据。 */
#define ROM_CS_ON_MCU   1

#if ROM_CS_ON_MCU
#define ROM_CS          P23
#define ROM_CS_LOW()    (ROM_CS = 0)
#define ROM_CS_HIGH()   (ROM_CS = 1)
#else
#define ROM_CS_LOW()    do { } while (0)
#define ROM_CS_HIGH()   do { } while (0)
#endif

/* ---------------- 屏幕参数 ---------------- */
#define LCD_PAGES       8
#define LCD_WIDTH       128

/* 对比度（电子音量），黑屏或太淡先调这个：0x00~0x3F，常用 0x20~0x2C */
#define LCD_CONTRAST    0x28

/* ---------------- 字库地址常量（晶联讯官方例程口径） ---------------- */
/* 25Q80 里前端放的是半角/符号区，汉字区从第 846 个字开始，所以要加偏移。
   注意汉字区码起点是 **0xB0**（一级汉字），不是 0xA1。 */
#define GB16_OFFSET     846UL
#define ASCII8_BASE     0x3CF80UL

/* 25 系列 SPI Flash 读命令是 0x03 + 24 位地址，**后面直接就是数据，没有 dummy 字节**。
   个别字库芯片需要 1 字节 dummy，用 Font_SetDummy(1) 在线试。 */
#define FONT_READ_CMD   0x03
#define FONT_DUMMY      0

/* ---------------- LCD 底层 ---------------- */
void LCD_Init(void);
void LCD_Backlight(bit on);
void LCD_WriteCmd(unsigned char cmd);
void LCD_WriteData(unsigned char dat);
void LCD_SetPos(unsigned char page, unsigned char col);
void LCD_Clear(void);
void LCD_FillPage(unsigned char page, unsigned char dat);

/* ---------------- 字符绘制 ---------------- */
void LCD_PutASCII(unsigned char page, unsigned char col, unsigned char ch);
void LCD_PutString(unsigned char page, unsigned char col, unsigned char *s);
void LCD_PutHanzi(unsigned char page, unsigned char col, unsigned char hi, unsigned char lo);
void LCD_PutHanziString(unsigned char page, unsigned char col, unsigned char *gb);

/* 从 code 区贴图：w 列 x 2 页(16 行)，数据按"每列 2 字节、先上后下" */
void LCD_PutTile(unsigned char page, unsigned char col, unsigned char w, unsigned char code *tile);

/* ---------------- 字库 ---------------- */
unsigned long Font_AddrGB2312(unsigned char hi, unsigned char lo);
unsigned long Font_AddrASCII(unsigned char ch);
bit Font_Read(unsigned long addr, unsigned char *buf, unsigned char len);
void Font_SetDummy(unsigned char n);
unsigned char Font_Dummy(void);

/* 字库自检：打印几种候选地址读到的数据 */
void Font_Probe(void);

/* 串口命令循环：接收 [ ... ] 并显示到屏上（不返回） */
void LCD_SerialLoop(void);

/* ---------------- 测试（实现在 lcd12864_test.c） ---------------- */
void LCD_TestAll(void);

#endif /* __LCD12864_H__ */
