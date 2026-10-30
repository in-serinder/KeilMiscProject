#include "stc90c58.h"
#include "Display\d12864.h"

/*
 *  JLX12864G-086-PC 汉字显示测试
 *  LCD驱动: UC1701X (模拟SPI, 引脚见 d12864.h)
 *  字库:   JLX-GB2312 独立ROM (引脚见 d12864.h)
 *  字号:   16x16大汉字 / 8x16中字符 / 5x7小ASCII
 *
 *  硬件接线:
 *    LCD:  CS=P3.7  RST=P3.6  AO=P3.5  LEDA=P3.4  SDA=P2.6  SCL=P2.5
 *    ROM:  SI=P2.7  SO=P0.6   SCK=P0.5  CS=P1.5(固定低, 硬件接地)
 */

/* GB2312 编码的测试字符串(用十六进制避免编码依赖)
 * "测试" GB2312: 测=0xB2,0xE2  试=0xCA,0xD4
 * "我是傻鸟" GB2312:
 *   我=0xCE,D2  是=0xCA,C0  傻=0xC9,B1  鸟=0xC4,FA
 * 注意: 字符串必须以 0x00 结尾! */
uchar code test_gb[]    = {0xB2, 0xE2, 0xCA, 0xD4, 0x00};  /* "测试" */
uchar code woshan_gb[]  = {0xCE, 0xD2, 0xCA, 0xC0, 0xC9, 0xB1, 0xC4, 0xFA, 0x00};  /* "我是傻鸟" */

void main(void)
{
    uchar i;
    uchar rom_diag[16];

    initial_lcd();
    clear_screen();

    /* ===== 诊断: 读字库ROM地址0~15的原始数据 =====
     * 正常字库这些地址有非0xFF非0x00的实际数据.
     * 如果全0xFF -> ROM_OUT(P0.6)未接或字库无响应.
     * 如果全0x00 -> ROM_OUT被拉低或CS未生效.
     * 有变化 -> 通讯正常, 问题在地址计算. */
    for (i = 0; i < 16; i++)
        rom_diag[i] = rom_simple_read((ulong)i);
    /* 以位图显示在屏幕第1页(128列), 全0xFF则全黑, 全0x00则全白 */
    lcd_address(1, 1);
    for (i = 0; i < 16; i++)
        transfer_data_lcd(rom_diag[i]);
    delay(3000);
    clear_screen();

    while (1)
    {
        /* ===== 大号 16x16: "测试" ===== */
        show_str_16x16(1, 1, (uchar *)test_gb);

        /* ===== 中号 8x16: "我是傻鸟" ===== */
        show_str_8x16(4, 1, (uchar *)woshan_gb);

        /* ===== 小号 5x7: ASCII ===== */
        show_str_5x7(7, 1, "TEST 2025");

        delay(4000);

        /* ===== 混合中英文 ===== */
        clear_screen();
        show_str_16x16(1, 1, (uchar *)test_gb);
        show_str_8x16(4,  1, (uchar *)woshan_gb);
        show_str_5x7(7,  1, "ABC 123 abc");
        delay(4000);
    }
}