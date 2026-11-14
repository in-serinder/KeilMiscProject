#include "stc90c58.h"
#include "Display\d12864.h"

/*
 *  JLX12864G-08602 Ver4.0  128x64 点阵显示
 *  LCD 驱动: UC1701X (模拟 SPI, 引脚见 d12864.h)
 *
 *  两种出字方式, 开关在 Display/d12864.h, 8K Flash 只能二选一:
 *    USE_ASCII_FONT = 1 : 内置 5x7 ASCII 点阵, 不依赖模块字库 IC  (当前配置)
 *    USE_FONT_ROM   = 1 : 走模块上的 25Q80 字库 IC, 可显示 16x16 汉字
 *
 *  硬件接线:
 *    LCD:  CS=P3.7  RST=P3.6  AO=P3.5  LEDA=P3.4  SDA=P2.6  SCL=P2.5   正常
 *    ROM:  SI=P2.7  SO=P0.6   SCK=P0.5  (R2/R4 各 5.1k 上拉到 +5V)     正常
 *    ROM:  CS#= 模块第4脚, 板上直接接地, MCU 无连线                     << 缺这一根
 */

#if USE_ASCII_FONT

/* ---- 演示文本 (纯 ASCII) ---- */
uchar code line_title[] = "HELLO WORLD";
uchar code line1[]      = "8051 + 12864 LCD";
uchar code line2[]      = "NO FONT ROM NEEDED";
uchar code line3[]      = "BUILT-IN 5X7 ASCII";
uchar code line4[]      = "MAX 21 CHARS A LINE";

uchar code line5[]      = "The quick brown fox";
uchar code line6[]      = "jumps over the lazy";
uchar code line7[]      = "dog. 0123456789";
uchar code line8[]      = "!@#$%^&*()_+-=[]{}";

void main(void)
{
    initial_lcd();
    clear_screen();

    while (1)
    {
        clear_screen();

        /* 第 1~2 页: 2 倍放大标题 (10x14, 每行 11 个字) */
        lcd_text_2x(1, 1, line_title);

        /* 第 4~7 页: 5x7 小字, 每行 21 个字 */
        lcd_text(4, 1, line1);
        lcd_text(5, 1, line2);
        lcd_text(6, 1, line3);
        lcd_text(7, 1, line4);

        delay(5000);

        /* 第二屏: 大小写混排 */
        clear_screen();
        lcd_text_2x(1, 1, "ASCII 5X7");
        lcd_text(4, 1, line5);
        lcd_text(5, 1, line6);
        lcd_text(6, 1, line7);
        lcd_text(7, 1, line8);

        delay(5000);
    }
}

#elif USE_FONT_ROM

/* GB2312 编码的测试字符串(用十六进制避免源文件编码依赖)
 * "测试": 测=0xB2,0xE2  试=0xCA,0xD4
 * "我是傻鸟": CE D2 / CA C0 / C9 B1 / C4 FA */
uchar code test_gb[]   = {0xB2, 0xE2, 0xCA, 0xD4, 0x00};
uchar code woshan_gb[] = {0xCE, 0xD2, 0xCA, 0xC0, 0xC9, 0xB1, 0xC4, 0xFA, 0x00};

void main(void)
{
    initial_lcd();
    clear_screen();

    /* 上电后依次: 3s 静态电平窗口(全黑) -> 3s(全白) -> 1s 竖条纹 -> 状态方块
     * 全实心才会往下走, 否则停在诊断页 */
    if (font_selftest() != 0)
    {
        while (1) { }
    }

    delay(2000);
    clear_screen();

    while (1)
    {
        show_str_16x16(1, 1, (uchar *)test_gb);
        show_str_8x16 (4, 1, (uchar *)woshan_gb);
        show_str_5x7  (7, 1, "TEST 2026");
        delay(4000);

        clear_screen();
        show_str_16x16(1, 1, (uchar *)test_gb);
        show_str_8x16 (4, 1, (uchar *)woshan_gb);
        show_str_5x7  (7, 1, "ABC 123 abc");
        delay(4000);
    }
}

#endif
