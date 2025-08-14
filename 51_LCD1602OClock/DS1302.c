#include <DS1302.h>
#include <Timer.h>

sbit RST = P1 ^ 3;
sbit IO = P1 ^ 2;
sbit SCK = P1 ^ 1;

/*
DS1302内部寄存器
CH:时钟停止位寄存器2的第7位12/24小时标志
CH=0振荡器工作允许bit7=1,12小时模式
CH=1振荡器停止bit7=0,24小时模式
WP: 写保护位寄存器2的第5位:AM/PM定义
WP=0寄存器数据能够写入 AP=1   下午模式
WP=1寄存器数据不能写入 AP=0   上午模式
     TCS: 涓流充电选择                    DS: 二极管选择位
     TCS=1010    使能涓流充电            DS=01     选择一个二极管
     TCS=其它    禁止涓流充电            DS=10     选择两个二极管
     DS=00或11, 即使TCS=1010, 充电功能也被禁
*/

#define ds1302_sec_add 0x80     // 秒
#define ds1302_min_add 0x82     // 分
#define ds1302_hr_add 0x84      // 时
#define ds1302_date_add 0x86    // 日
#define ds1302_month_add 0x88   // 月
#define ds1302_day_add 0x8a     // 周
#define ds1302_year_add 0x8c    // 年
#define ds1302_control_add 0x8e // 数据控制
#define ds1302_charger_add 0x90
#define ds1302_clkburst_add 0xbe

unsigned char time_buf[8] = {0x20, 0x10, 0x09, 0x14, 0x23, 0x59, 0x50, 0x02}; // 日期以十六进制存储
char TimeStr[18];
// unsigned char readtime[14];
// uchar sec_buf = 0;
// uchar sec_flag = 0;

void DS1302_Init()
{
    RST = 0;
    SCK = 0;
}

void writeByte(unsigned char addr, unsigned char dat)
{
    unsigned char i;
    RST = 0; // 启动总线

    // 写入到指定地址

    addr &= 0xFE; // 最低位至0，0写1读

    for (i = 0; i < 8; i++)
    {
        if (addr & 0x01)
        {
            IO = 1;
        }
        else
        {
            IO = 0;
        }

        SCK = 1;
        SCK = 0; // 产生一个时钟

        addr = addr >> 1; // 右移
    }

    // 写入数据
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x01)
        {
            IO = 1;
        }
        else
        {
            IO = 0;
        }
        SCK = 1;
        SCK = 0;
        dat = dat >> 1;
    }
    RST = 0;
}

unsigned char readByte(unsigned addr)
{
    unsigned char i, dat;
    RST = 1;

    addr |= 0x01;

    for (i = 0; i < 8; i++)
    {
        if (addr & 0x01)
        {
            IO = 1;
        }
        else
        {
            IO = 0;
        }
        SCK = 1;
        SCK = 0;

        addr = addr >> 1;
    }

    // 输出到temp
    for (i = 0; i < 8; i++)
    {
        dat = dat >> 1; // 右移存储
        if (IO)
        {
            dat |= 0x80;
        }
        else
        {
            dat &= 0x7F;
        }
        SCK = 1;
        SCK = 0;
    }
    RST = 0;
    return dat;
}

void DS1302WriteTime()
{
    writeByte(ds1302_control_add, 0x00); // 关闭写保护
    writeByte(ds1302_sec_add, 0x80);
    // writeByte(ds1302_charger_add,0xa9); //涓流充电
    writeByte(ds1302_year_add, time_buf[1]);
    writeByte(ds1302_month_add, time_buf[2]);
    writeByte(ds1302_date_add, time_buf[3]);
    writeByte(ds1302_hr_add, time_buf[4]);
    writeByte(ds1302_min_add, time_buf[5]);
    writeByte(ds1302_sec_add, time_buf[6]);
    writeByte(ds1302_day_add, time_buf[7]);
    writeByte(ds1302_control_add, 0x80);
}

void readTime()
{
    time_buf[1] = readByte(ds1302_year_add);
    time_buf[2] = readByte(ds1302_month_add);
    time_buf[3] = readByte(ds1302_date_add);
    time_buf[4] = readByte(ds1302_hr_add);
    time_buf[5] = readByte(ds1302_min_add);
    time_buf[6] = readByte(ds1302_sec_add) & 0x7f; // 直接读取秒寄存器地址
    time_buf[7] = readByte(ds1302_day_add);
}

char *getTimerString()
{
    // unsigned char i;
    readTime();
    // // 年
    // TimeStr[0] = (time_buf[0] >> 4);
    // TimeStr[1] = (time_buf[0] & 0x0F);

    // TimeStr[2] = (time_buf[1] >> 4);
    // TimeStr[3] = (time_buf[1] & 0x0F);
    // TimeStr[4] = '-';

    // // 月
    // TimeStr[5] = (time_buf[1] >> 4);
    // TimeStr[6] = (time_buf[1] & 0x0F);
    // TimeStr[7] = '-';
    // // 日
    // TimeStr[8] = (time_buf[1] >> 4);
    // TimeStr[9] = (time_buf[1] & 0x0F);
    // TimeStr[10] = '-';
    // // 时
    // TimeStr[11] = (time_buf[1] >> 4);
    // TimeStr[12] = (time_buf[1] & 0x0F);
    // TimeStr[13] = ':';
    // // 分
    // TimeStr[14] = (time_buf[1] >> 4);
    // TimeStr[15] = (time_buf[1] & 0x0F);
    // TimeStr[16] = ':';
    // // 秒
    // TimeStr[17] = (time_buf[1] >> 4);
    // TimeStr[18] = (time_buf[1] & 0x0F);

    // 年

    TimeStr[0] = (time_buf[1] >> 4) + '0';
    TimeStr[1] = (time_buf[1] & 0x0F) + '0';
    TimeStr[2] = '-';

    // 月
    TimeStr[3] = (time_buf[2] >> 4) + '0';
    TimeStr[4] = (time_buf[2] & 0x0F) + '0';
    TimeStr[5] = '-';
    // 日
    TimeStr[6] = (time_buf[3] >> 4) + '0';
    TimeStr[7] = (time_buf[3] & 0x0F) + '0';
    TimeStr[8] = '-';
    // 时
    TimeStr[9] = (time_buf[4] >> 4) + '0';
    TimeStr[10] = (time_buf[4] & 0x0F) + '0';
    TimeStr[11] = ':';
    // 分
    TimeStr[12] = (time_buf[5] >> 4) + '0';
    TimeStr[13] = (time_buf[5] & 0x0F) + '0';
    TimeStr[14] = ':';
    // 秒
    TimeStr[15] = (time_buf[6] >> 4) + '0';
    TimeStr[16] = (time_buf[6] & 0x0F) + '0';

    TimeStr[17] = '\0';

    return TimeStr;
}