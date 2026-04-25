// #include <REG52.H>
#include <Max7219.h>
#include <Ser.h>
#include <string.h>

sbit MAX7219_CLK = P2 ^ 0;
sbit MAX7219_DIN = P2 ^ 1;
sbit MAX7219_LOAD = P2 ^ 2; // 加载信号（片选）

// 通过7219内部寄存器控制LED
// Load处于低电平有效状态
// 先将LOAD置高电平，再将数据写入7219，最后将LOAD置低电平 片选信号

// 修正：删除函数声明，直接定义函数

void Max7219_SendByte(unsigned char dat)
{
    unsigned char i;
    for (i = 0; i < 8; i++) // 每个片选（管位）寄存器（每位一个8位二进制的数据）
    {
        MAX7219_CLK = 0;                    // 先将CLK置低电平
        MAX7219_DIN = (dat & 0x80) ? 1 : 0; // 先发送高位
        dat <<= 1;                          // 左移一位，将下一位放到最高位
        MAX7219_CLK = 1;                    // 再将CLK置高电平,一位数据发送完成
        Delay_us(1);                        // 延时，等待数据稳定
    }
}

void Max7219_Write(unsigned char address, unsigned char dat)
{
    MAX7219_LOAD = 0;
    Max7219_SendByte(address);
    Max7219_SendByte(dat);
    MAX7219_LOAD = 1; // 完成传输
    Delay_us(1);
}

void Max7219_Init()
{

    Delay_ms(10);
    // 使用正确的函数名和参数
    Max7219_Write(0x0C, 0x01); // 退出休眠模式
    Max7219_Write(0x0F, 0x00); // 关闭测试模式
    Max7219_Write(0x0B, 0x07); // 设置扫描界限为8位（DIG0~DIG7）
    Max7219_Write(0x09, 0xFF); // 启用BCD译码（简化数字显示）
    Max7219_Write(0x0A, 0x08); // 设置亮度（0~15，8为中等亮度）
}

void Max7219_Display(unsigned char digit, unsigned char value, unsigned char dot_v)
{
    Max7219_Write(digit, value | (dot_v ? 0x80 : 0x00));
}

void Max7219_Clear()
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        Max7219_Write(i + 1, 0x00);
    }
}

void Max7219_NumDisplay(double num)
{
    unsigned char i;
    unsigned int integer_part;               // 整数部分
    unsigned int decimal_part;               // 小数部分（取3位）
    unsigned char xdata int_digits[8] = {0}; // 存储整数每一位
    unsigned char xdata dec_digits[3] = {0}; // 存储小数每一位
    unsigned char int_len = 0;               // 整数位数

    if (num < 0)
    {
        Max7219_Display(1, 0x0A, 0); // 显示负号（0x0A对应'-'，需关闭BCD译码）
        num = -num;
        int_len = 1;
    }

    // 分离整数和小数部分
    integer_part = (unsigned int)num;
    decimal_part = (unsigned int)((num - integer_part) * 1000); // 保留3位小数

    if (integer_part == 0)
    {
        int_digits[int_len++] = 0; // 整数为0时直接记录
    }
    else
    {
        while (integer_part > 0)
        {
            int_digits[int_len++] = integer_part % 10;
            integer_part /= 10; // 移除最低位
        }
    }

    dec_digits[0] = (decimal_part / 100) % 10; // 第1位小数
    dec_digits[1] = (decimal_part / 10) % 10;  // 第2位小数
    dec_digits[2] = decimal_part % 10;         // 第3位小数

    // 显示整数部分（从高位到低位，位置从int_len开始）
    for (i = 0; i < int_len; i++)
    {
        // 位置：负号占1位时，整数从2开始；否则从1开始
        unsigned char pos = (int_len > 1 && int_digits[int_len - 1] == 0x0A) ? (i + 2) : (i + 1);

        unsigned char dot = (i == int_len - 1) ? 1 : 0;
        Max7219_Display(pos, int_digits[int_len - 1 - i], dot);
    }

    // 显示小数部分（接在整数后面）
    for (i = 0; i < 3; i++)
    {
        unsigned char pos = int_len + 1 + i; // 小数部分起始位置
        if (pos > 8)
            break;
        Max7219_Display(pos, dec_digits[i], 0);
    }
}
// void Max7219StringDisplay(char *str)
// {
//     unsigned char idata i = 0;
//     unsigned char idata pos = 1; // Max7219有效位置：1~8

//     bit dot = 0;

//     while (str[i] != '\0' && pos <= 8)
//     {

//         if (str[i + 1] == '.')
//         {
//             dot = 1;
//             i++; // 跳过小数点
//         }

//         if (str[i] >= '0' && str[i] <= '9')
//         {
//             Max7219_Display(9 - pos, (str[i] - '0'), dot); // 右到左
//             pos++;                                         // 存在显示问题、、、、、

//             UART_SendString("start");

//             UART_SendChar('\n');

//             UART_SendChar(str[0]);
//             UART_SendChar('\n');
//             UART_SendString("end");

//             // UART_SendString("err");
//             UART_SendChar('0' + strlen(str));
//             if (pos > 8 && pos < 1)
//             {
//                 UART_SendString("err");
//             }
//         }

//         dot = 0;
//         i++;
//     }

//     while (pos <= 8)
//     {
//         Max7219_Display(pos++, 0x0F, 0); // 0x0F：熄灭该位
//     }
// }

void Max7219DateDisplay(char *datestr)
{

    unsigned char i;
    // Max7219_Clear();
    // insertCharInString(datestr, 4, '.');

    // insertCharInString(datestr, 7, '.');

    for (i = 0; i < 8; i++)
    {

        Max7219_Display(8 - i, datestr[i], (i == 3 || i == 5) ? 1 : 0);
    }

    // UART_SendChar('\n');
    // Max7219StringDisplay(datestr);
    // UART_SendString(datestr);
    // UART_SendString("\r\n");
}

void Max7219TemperatureAndHumitidyDisplay(unsigned char *temp, unsigned char *hum)
{
    char xdata th[11];
    unsigned char i;

    sprintf(th, "%s%s", temp, hum);
    removeChars(th, ".");

    UART_SendChar('\n');
    // Max7219StringDisplay(datestr);
    UART_SendString(th);
    UART_SendString("\r\n");

    for (i = 0; i < 8; i++)
    {
        Max7219_Display(8 - i, th[i], (i == 5 || i == 1) ? 1 : 0);
    }
}