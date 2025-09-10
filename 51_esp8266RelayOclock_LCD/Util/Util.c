#include <string.h>
#include <Util.h>

/* ----串口消息格式----
 *时间戳: 20231024^160000^
 *继电器状态 000^
 *温湿度 00.00^00.00^
 *完全格式 >20231024^160000^000^00.00^00.00^
 */

// struct Util dat = {
//     .Time = "160000",
//     .Date = "20231024",
//     .Temperature = 25.5,
//     .Humidity = 60.0,
//     .Relay1 = 0,
//     .Relay2 = 0,
//     .Relay3 = 0};

sbit BuzzerS2 = P0 ^ 6;

struct UtilOBJ idata dat = {
    "160000",   // Time
    "20231024", // Date
    25.5,       // Temperature
    60.0,       // Humidity
    0,          // Relay1
    0,          // Relay2
    0,          // Relay3
    0           // warn
};

unsigned char xdata infraredReceivedData[4];
unsigned char irtime;
bit startflag = 0;
bit irpro_ok, irok;
unsigned char irBitCount = 0;

void separateData(char *str)
{
    unsigned char i;
    // unsigned int temp = 0, hum = 0;
    // unsigned char temperature[6];
    // unsigned char humidity[6];
    *str++; // 去掉首标识>
    // while (*str++ != '\0')
    // {
    // }

    for (i = 0; i < 8 && *str != '\0'; i++)
    {
        dat.Date[i] = *str++;
    }
    dat.Date[i] = '\0';
    *str++; // 去掉间隔标识>

    for (i = 0; i < 6 && *str != '\0'; i++)
    {
        dat.Time[i] = *str++;
    }

    dat.Time[i] = '\0';
    *str++;

    dat.Relay1 = *str++;
    dat.Relay2 = *str++;
    dat.Relay3 = *str++;
    // dat.Relay1 = (*str++ == 1);
    // dat.Relay2 = (*str++ == 1);
    // dat.Relay3 = (*str++ == 1);

    *str++; // 去掉间隔标识>

    for (i = 0; i < 5 && *str != '\0'; i++)
    {
        // temperature[i] = *str++;
        dat.Temperature[i] = *str++;
    }
    dat.Temperature[i] = '\0';
    // temperature[i] = '\0';
    // dat.Temperature = atof(temperature);
    *str++; // 去掉间隔标识>

    for (i = 0; i < 5 && *str != '\0'; i++)
    {
        // humidity[i] = *str++;
        dat.Humidity[i] = *str++;
    }
    dat.Humidity[i] = '\0';
    *str++; // 去掉间隔标识>

    // 警告标识
    dat.WarnFlag = *str++;

    // humidity[i] = '\0';

    // dat.Humidity = atof(humidity);

    // for (i = 0; temperature[i] != '\0'; i++)
    // {
    //     if (temperature[i] == '.')
    //         continue;
    //     temp = temp * 10 + (temperature[i] - '0');
    // }
    // for (i = 0; humidity[i] != '\0'; i++)
    // {
    //     if (humidity[i] == '.')
    //         continue;
    //     hum = hum * 10 + (humidity[i] - '0');
    // }
    // dat.Temperature = temp / 100.0; // 假设精度为两位小数
    // dat.Humidity = hum / 100.0;
}

struct UtilOBJ getUtil(char *str)
{
    separateData(str);
    return dat;
}

// void insertCharInString(char *str, unsigned char pos, char ch)
// {
//     // static char buffer[15];
//     unsigned char len = strlen(str);
//     unsigned char i;

//     if (pos < 0 || pos > len)
//         return;

//     // 插入位后字符后移
//     for (i = len; i >= pos; i--)
//     {
//         str[i + 1] = str[i];
//     }

//     str[pos] = ch; // 插入字符

//     // return str;
// }

void InitExternalInterrupt0()
{
    IT0 = 1; // 设置INT0为下降沿触发（IT0=1）
    EX0 = 1; // 使能外部中断0
    EA = 1;  // 开总中断
}

void infraredReceive() interrupt 0
{
    TR0 = 0; // 关闭定时器，防止计时误差

    if (startflag)
    {
        // 接收引导码后的32位数据
        if (irtime > 32)
        {                                       // 引导码之后的第一个下降沿
            irBitCount = 0;                     // 重置位计数器
            memset(infraredReceivedData, 0, 4); // 清空接收缓冲区
        }
        else if (irBitCount < 32)
        { // 接收32位数据
            unsigned char byteIndex = irBitCount / 8;
            unsigned char bitIndex = irBitCount % 8;

            // 根据irtime判断是0还是1
            if (irtime >= 8)
            { // 高电平时间>8*500us=4ms，表示接收到'1'
                infraredReceivedData[byteIndex] |= (1 << bitIndex);
            }

            irBitCount++;

            // 判断是否接收完32位
            if (irBitCount == 32)
            {
                // 验证地址码和命令码（取反校验）
                if ((infraredReceivedData[0] == ~infraredReceivedData[1]) &&
                    (infraredReceivedData[2] == ~infraredReceivedData[3]))
                {
                    irpro_ok = 1; // 数据有效，设置完成标志
                }
            }
        }
    }
    else
    {
        // 接收引导码
        startflag = 1;
    }

    irtime = 0; // 重置定时器值
    TR0 = 1;    // 重新启动定时器
}

void timer() interrupt 1
{
    irtime++;
}

bit getIRFlag()
{
    return irpro_ok;
}

unsigned char *getIRData()
{
    return infraredReceivedData;
}

void removeChars(char *str, const char *targets)
{
    char *src = str;
    char *dst = str;

    while (*src)
    {
        const char *t = targets;
        int isTarget = 0;

        // 检查是否为目标字符
        while (*t)
        {
            if (*src == *t)
            {
                isTarget = 1;
                break;
            }
            t++;
        }

        if (!isTarget)
        {
            *dst = *src;
            dst++;
        }
        src++;
    }
    *dst = '\0';
}

void RelayBuzzer()
{
    BuzzerS2 = 1;
    Delay_ms(1000);
    BuzzerS2 = 0;
}

unsigned int charToint(unsigned char ch)
{
    return ch - '0';
}
