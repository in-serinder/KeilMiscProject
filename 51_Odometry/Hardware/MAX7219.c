#include "stc89c52.h"
#include "MAX7219.h"
#include "Delay.h"
#include <string.h>

#define MAX7219_BLANK 0x0F

// #define MAX7219_POS_REVERSED   // 如需反向显示请取消注释

static uint8_t PosMap(uint8_t p)
{
#ifdef MAX7219_POS_REVERSED
    return 9 - p;
#else
    return p;
#endif
}


void Max7219_SendByte(uint8_t dat)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        MAX7219_CLK = 0;
        MAX7219_DIN = (dat & 0x80) ? 1 : 0;
        dat <<= 1;
        MAX7219_CLK = 1;
        Delay_us(1);   
    }
}


void Max7219_Write(uint8_t address, uint8_t dat)
{
    MAX7219_LOAD = 0;
    Delay_us(1);
    Max7219_SendByte(address);
    Max7219_SendByte(dat);
    
    MAX7219_CLK = 0;
    Delay_us(1);
    MAX7219_LOAD = 1;     
    Delay_us(5);          
}


void Max7219_Init(void)
{
    uint8_t i;

    MAX7219_LOAD = 1;
    MAX7219_CLK  = 0;
    MAX7219_DIN  = 0;
    Delay_ms(20);

    /* 配置寄存器 */
    Max7219_Write(0x0C, 0x01);   // Shutdown: 退出休眠
    Max7219_Write(0x0F, 0x00);   // Display Test: 关闭
    Max7219_Write(0x0B, 0x07);   // Scan Limit: 扫描8位
    Max7219_Write(0x09, 0xFF);   // Decode Mode: 全部BCD译码
    Max7219_Write(0x0A, 0x08);   // Intensity: 中等亮度

    for (i = 1; i <= 8; i++)
        Max7219_Display(i, 8, 1);
    Delay_ms(200);
    Max7219_Clear();
    Delay_ms(100);

    // for (i = 1; i <= 8; i++)
    //     Max7219_Display(i, i, 0);
    // Delay_ms(500);
    // Max7219_Clear();
    // Delay_ms(50);
}


void Max7219_Display(uint8_t digit, uint8_t value, uint8_t dot_v)
{
    Max7219_Write(digit, value | (dot_v ? 0x80 : 0x00));
}

void Max7219_DisplayBlank(uint8_t digit, uint8_t dot_v)
{
    Max7219_Write(digit, MAX7219_BLANK | (dot_v ? 0x80 : 0x00));
}

void Max7219_Clear(void)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        Max7219_Write(i + 1, 0x00);
    }
}

static void WriteRange(uint8_t left_pos, uint8_t right_pos,
                       uint32_t scaled, uint8_t format, bit neg)
{
    uint8_t total = right_pos - left_pos + 1;
    uint8_t xdata dig[8];
    uint8_t xdata dot[8];
        uint8_t i, fill;
    uint8_t phys;
    for (i = 0; i < 8; i++) { dig[i] = 0; dot[i] = 0; }

    fill = total - 1;
    do {
        dig[fill] = (uint8_t)(scaled % 10);
        if (format != 0 && fill == total - 1 - format) dot[fill] = 1;
        scaled /= 10;
        if (fill == 0) break;
        fill--;
    } while (scaled > 0);

    /* 剩余左边位置熄灭（消隐），实现右对齐 */
    for (i = 0; i < fill; i++)
        dig[i] = MAX7219_BLANK;

        if (neg && fill > 0) {
        fill--;
        dig[fill] = 0x0A;
    }

    {
        uint8_t first_nonzero = total;
        for (i = 0; i < total; i++) {
            if (dig[i] != MAX7219_BLANK && dig[i] != 0 && dig[i] != 0x0A) {
                first_nonzero = i;
                break;
            }
        }
        /* 全是0 → 保留最后一个位置显示0 */
        if (first_nonzero == total) first_nonzero = total - 1;
        /* 把第一个有效数字左边的0也熄灭 */
        for (i = 0; i < first_nonzero; i++) {
            if (dig[i] == 0) {
                dig[i] = MAX7219_BLANK;
                dot[i] = 0;
            }
        }
    }

    for (i = 0; i < total; i++) {
        phys = PosMap(left_pos + i);
        if (dig[i] == MAX7219_BLANK)
            Max7219_DisplayBlank(phys, dot[i]);
        else
            Max7219_Display(phys, dig[i], dot[i]);
    }
}

void Max7219_DisplayFloat(float value, uint8_t format)
{
    uint32_t scaled;
    bit neg = 0;
    uint8_t i;
    uint32_t mul = 1;
    if (format > 7) format = 7;
    if (value < 0) { neg = 1; value = -value; }
    for (i = 0; i < format; i++) mul *= 10;
    scaled = (uint32_t)(value * (float)mul + 0.5f);
    WriteRange(1, 8, scaled, format, neg);
}

void Max7219_DisplayFloatSplit(float value1, float value2, uint8_t format)
{
    uint32_t s1, s2;
    bit n1 = 0, n2 = 0;
    uint8_t i;
    uint32_t mul = 1;
    if (format > 3) format = 3;
    if (value1 < 0) { n1 = 1; value1 = -value1; }
    if (value2 < 0) { n2 = 1; value2 = -value2; }
    for (i = 0; i < format; i++) mul *= 10;
    s1 = (uint32_t)(value1 * (float)mul + 0.5f);
    s2 = (uint32_t)(value2 * (float)mul + 0.5f);
        WriteRange(1, 4, s1, format, n1);
    WriteRange(5, 8, s2, format, n2);
}


/* 左右双值显示，左右可用不同小数位数 */
void Max7219_DisplayDualFmt(float left_val, uint8_t left_fmt,
                           float right_val, uint8_t right_fmt)
{
    uint32_t sl, sr;
    bit nl = 0, nr = 0;
    uint8_t i;
    uint32_t mul;

    if (left_fmt > 3)  left_fmt = 3;
    if (right_fmt > 3) right_fmt = 3;

    if (left_val < 0)  { nl = 1; left_val = -left_val; }
    if (right_val < 0) { nr = 1; right_val = -right_val; }

    mul = 1;
    for (i = 0; i < left_fmt; i++) mul *= 10;
    sl = (uint32_t)(left_val * (float)mul + 0.5f);

    mul = 1;
    for (i = 0; i < right_fmt; i++) mul *= 10;
    sr = (uint32_t)(right_val * (float)mul + 0.5f);

    WriteRange(1, 4, sl, left_fmt, nl);
    WriteRange(5, 8, sr, right_fmt, nr);
}


#if 0
void Max7219_NumDisplay(double num)
{
    uint8_t i;
    uint32_t xdata integer_part;
    uint16_t xdata decimal_part;
    uint8_t xdata int_digits[8] = {0};
    uint8_t xdata dec_digits[3] = {0};
    uint8_t int_len = 0;
    uint8_t xdata pos, dot;

    Max7219_Clear();
    if (num < 0) {
        Max7219_Display(PosMap(1), 0x0A, 0);
        num = -num;
        int_len = 1;
    }
    integer_part = (uint32_t)num;
    decimal_part = (uint16_t)((num - (double)integer_part) * 1000.0);
    if (integer_part == 0) {
        int_digits[int_len++] = 0;
    } else {
        while (integer_part > 0) {
            int_digits[int_len++] = (uint8_t)(integer_part % 10);
            integer_part /= 10;
        }
    }
    dec_digits[0] = (decimal_part / 100) % 10;
    dec_digits[1] = (decimal_part / 10) % 10;
    dec_digits[2] = decimal_part % 10;
    for (i = 0; i < int_len; i++) {
        pos = (int_len > 1 && int_digits[int_len - 1] == 0x0A) ? (i + 2) : (i + 1);
        dot = (i == int_len - 1) ? 1 : 0;
        Max7219_Display(PosMap(pos), int_digits[int_len - 1 - i], dot);
    }
    for (i = 0; i < 3; i++) {
        pos = int_len + 1 + i;
        if (pos > 8) break;
        Max7219_Display(PosMap(pos), dec_digits[i], 0);
        }
}
#endif


#if 0
void Max7219DateDisplay(char *datestr)
{
    uint8_t i;
    Max7219_Clear();
    for (i = 0; i < 8; i++) {
        Max7219_Display(PosMap(8 - i), datestr[i], (i == 3 || i == 5) ? 1 : 0);
        }
}
#endif



void Max7219_DisplayCustomSegments(const uint8_t *segs, uint8_t len)
{
    uint8_t i;
    Max7219_Clear();
    /* 切换到非译码模式 */
    Max7219_Write(0x09, 0x00);
    for (i = 0; i < 8; i++) {
        if (i < len)
            Max7219_Write(i + 1, segs[i]);
        else
            Max7219_Write(i + 1, SEG_BLANK);
    }
        /* 恢复BCD译码模式 */
    Max7219_Write(0x09, 0xFF);
}


#if 0
void removeChars(char *str, const char *remove)
{
    char *src = str;
    char *dst = str;
    while (*src) {
        const char *r = remove;
        bit skip = 0;
        while (*r) {
            if (*src == *r) { skip = 1; break; }
            r++;
        }
        if (!skip) *dst++ = *src;
        src++;
    }
    *dst = '\0';
}
#endif