#include "stc89c52.h"
#include "Max7219.h"
#include "Delay.h"

#include <string.h>

// sbit MAX7219_CLK  = P2 ^ 0;
// sbit MAX7219_DIN  = P2 ^ 1;
// sbit MAX7219_LOAD = P2 ^ 2;

#define MAX7219_BLANK 0x0F

static const uint8_t code DigitMap[8] = {8,7,6,5,4,3,2,1};

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
    Max7219_SendByte(address);
    Max7219_SendByte(dat);
    MAX7219_LOAD = 1;
    Delay_us(1);
}

void Max7219_Init()
{
    Delay_ms(10);
    Max7219_Write(0x0C, 0x01);
    Max7219_Write(0x0F, 0x00);
    Max7219_Write(0x0B, 0x07);
    Max7219_Write(0x09, 0xFF);
    Max7219_Write(0x0A, 0x08);
}

void Max7219_Display(uint8_t digit, uint8_t value, uint8_t dot_v)
{
    Max7219_Write(digit, value | (dot_v ? 0x80 : 0x00));
}

void Max7219_DisplayBlank(uint8_t digit, uint8_t dot_v)
{
    Max7219_Write(digit, MAX7219_BLANK | (dot_v ? 0x80 : 0x00));
}

void Max7219_Clear()
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        Max7219_Write(i + 1, 0x00);
    }
}

static uint8_t FloatToIntScale(float value, uint8_t format, signed long *int_val, uint8_t *neg)
{
     uint8_t i, scale = 1;
    uint8_t digits = 0;
    uint16_t xdata tmp;
    for (i = 0; i < format; i++)
    {
        scale *= 10;
    }
    if (value < 0)
    {
        *neg = 1;
        value = -value;
    }
    else
    {
        *neg = 0;
    }
    *int_val = (signed long)(value * scale + 0.5f);
    
    tmp = *int_val;
    if (tmp == 0)
    {
        digits = 1;
    }
    else
    {
        while (tmp > 0)
        {
            tmp /= 10;
            digits++;
        }
    }
    if (*neg) digits++;
    return digits;
}

static void DisplayScaledIntRange(uint8_t left_pos, uint8_t right_pos, signed long int_val, uint8_t format, uint8_t neg)
{
    uint8_t total_digits = right_pos - left_pos + 1;
    uint8_t xdata buf[8] = {0};
    uint8_t xdata dot_pos[8] = {0};
    uint8_t pos;
     uint8_t digit_count = 0;
    uint8_t i;
    uint16_t xdata tmp;
    uint8_t fill_idx;
    for (i = 0; i < total_digits; i++)
    {
        buf[i] = MAX7219_BLANK;
        dot_pos[i] = 0;
    }
    tmp = int_val;
     fill_idx= total_digits - 1;
   
    do
    {
        if (fill_idx < 0) break;
        buf[fill_idx] = (uint8_t)(tmp % 10);
        digit_count++;
        if (format > 0 && digit_count == format)
        {
            dot_pos[fill_idx] = 1;
        }
        tmp /= 10;
        fill_idx--;
    } while (tmp > 0);
    if (neg && fill_idx >= 0)
    {
        buf[fill_idx] = 0x0A;
        fill_idx--;
    }
    for (i = 0; i < total_digits; i++)
    {
         pos = DigitMap[left_pos - 1 + i];
        if (buf[i] == MAX7219_BLANK)
        {
            Max7219_DisplayBlank(pos, dot_pos[i]);
        }
        else
        {
            Max7219_Display(pos, buf[i], dot_pos[i]);
        }
    }
}

void Max7219_DisplayFloat(float value, uint8_t format)
{
    uint16_t xdata int_val;
    uint8_t neg;
    if (format > 7) format = 7;
    FloatToIntScale(value, format, &int_val, &neg);
    DisplayScaledIntRange(1, 8, int_val, format, neg);
}

void Max7219_DisplayFloatSplit(float value1, float value2, uint8_t format)
{
    uint16_t xdata int_val1, int_val2;
    uint8_t neg1, neg2;
    if (format > 3) format = 3;
    FloatToIntScale(value1, format, &int_val1, &neg1);
    FloatToIntScale(value2, format, &int_val2, &neg2);
    DisplayScaledIntRange(1, 4, int_val1, format, neg1);
    DisplayScaledIntRange(5, 8, int_val2, format, neg2);
}

void Max7219_NumDisplay(double num)
{
    uint8_t i;
    uint16_t xdata integer_part;
    uint16_t xdata decimal_part;
    uint8_t xdata int_digits[8] = {0};
    uint8_t xdata dec_digits[3] = {0};
    uint8_t int_len = 0;
  uint8_t xdata pos , dot;

    if (num < 0)
    {
        Max7219_Display(1, 0x0A, 0);
        num = -num;
        int_len = 1;
    }
    integer_part = (uint16_t)num;
    decimal_part = (uint16_t)((num - integer_part) * 1000);
    if (integer_part == 0)
    {
        int_digits[int_len++] = 0;
    }
    else
    {
        while (integer_part > 0)
        {
            int_digits[int_len++] = integer_part % 10;
            integer_part /= 10;
        }
    }
    dec_digits[0] = (decimal_part / 100) % 10;
    dec_digits[1] = (decimal_part / 10) % 10;
    dec_digits[2] = decimal_part % 10;
    for (i = 0; i < int_len; i++)
    {
         pos = (int_len > 1 && int_digits[int_len - 1] == 0x0A) ? (i + 2) : (i + 1);
         dot = (i == int_len - 1) ? 1 : 0;
        Max7219_Display(pos, int_digits[int_len - 1 - i], dot);
    }
    for (i = 0; i < 3; i++)
    {
         pos = int_len + 1 + i;
        if (pos > 8)
            break;
        Max7219_Display(pos, dec_digits[i], 0);
    }
}

void Max7219DateDisplay(char *datestr)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        Max7219_Display(8 - i, datestr[i], (i == 3 || i == 5) ? 1 : 0);
    }
}

void removeChars(char *str, const char *remove)
{
    char *src = str;
    char *dst = str;
    while (*src)
    {
        const char *r = remove;
        bit skip = 0;
        while (*r)
        {
            if (*src == *r)
            {
                skip = 1;
                break;
            }
            r++;
        }
        if (!skip)
        {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

