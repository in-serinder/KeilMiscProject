#include "DS1302.h"

// DS1302地址定义
#define _ds1302_sec_add 0x80      // 秒数据地址
#define _ds1302_min_add 0x82      // 分数据地址
#define _ds1302_hr_add 0x84       // 时数据地址
#define _ds1302_date_add 0x86     // 日数据地址
#define _ds1302_month_add 0x88    // 月数据地址
#define _ds1302_weekday_add 0x8a  // 星期数据地址
#define _ds1302_year_add 0x8c     // 年数据地址
#define _ds1302_control_add 0x8e  // 控制数据地址
#define _ds1302_charger_add 0x90  // 充电数据地址
#define _ds1302_clkburst_add 0xbe // 时钟突发数据地址

unsigned char ds1302_buf[8] =
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // 年 月 日 时 分 秒 星期 控制

unsigned char ntp_time[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

sbit CLK = P1 ^ 5;
sbit DAT = P1 ^ 6;
sbit RST = P1 ^ 7;

void DS1302_Init(void)
{
    RST = 0;
    CLK = 0;
}

// 寄存器0位为0时写，1时读
void DS1302_WriteByte(unsigned char addr, unsigned char dat)
{
    unsigned char i;
    RST = 1;
    addr &= 0xfe;
    // 上报寄存器地址
    for (i = 0; i < 8; i++)
    {
        if (addr & 0x01)
        {
            DAT = 1;
        }
        else
        {
            DAT = 0;
        }
        CLK = 1;
        Delay_us(20);
        CLK = 0;
        addr >>= 1;
    }
    // 写入dat
    for (i = 0; i < 8; i++)
    {

        if (dat & 0x01) // 按地位方向写
        {               // 高位1
            DAT = 1;
        }
        else
        { // 高位0
            DAT = 0;
        }
        CLK = 1;
        Delay_us(20);
        CLK = 0;
        dat >>= 1;
    }

    RST = 0;
}

void DS1302_ReadByte(unsigned char addr, unsigned char *dat)
{
    unsigned char i;
    RST = 1;
    addr |= 0x01; // 读模式地位写1
    // 上报寄存器地址
    for (i = 0; i < 8; i++)
    {
        if (addr & 0x01)
        {
            DAT = 1;
        }
        else
        {
            DAT = 0;
        }
        CLK = 1;
        Delay_us(20);
        CLK = 0;
        addr >>= 1;
    }
    // 读模式
    for (i = 0; i < 8; i++)
    {
        *dat >>= 1;
        if (DAT)
        {
            *dat |= 0x80;
        }
        else
        {
            *dat &= 0x7f;
        }
        CLK = 1;
        Delay_us(20);
        CLK = 0;
    }

    RST = 0;
}

/*
@brief 配置DS1302
@details 配置DS1302为工作模式，使能写保护,需预先配置ntp时间
@param 无
*/

// 高位1启用，0关闭
void DS1302_Config(void)
{
    DS1302_WriteByte(_ds1302_control_add, 0x00); // 写保护关闭
    DS1302_WriteByte(_ds1302_sec_add, 0x80);     // 暂停
    DS1302_WriteByte(_ds1302_year_add, ntp_time[0]);
    DS1302_WriteByte(_ds1302_month_add, ntp_time[1]);
    DS1302_WriteByte(_ds1302_date_add, ntp_time[2]);
    DS1302_WriteByte(_ds1302_hr_add, ntp_time[3]);
    DS1302_WriteByte(_ds1302_min_add, ntp_time[4]);
    DS1302_WriteByte(_ds1302_sec_add, ntp_time[5]);
    DS1302_WriteByte(_ds1302_control_add, 0x80); // 写保护关闭
}

void DS1302_ReadTime(void)
{
    DS1302_ReadByte(_ds1302_year_add, &ds1302_buf[0]);
    DS1302_ReadByte(_ds1302_month_add, &ds1302_buf[1]);
    DS1302_ReadByte(_ds1302_date_add, &ds1302_buf[2]);
    DS1302_ReadByte(_ds1302_hr_add, &ds1302_buf[3]);
    DS1302_ReadByte(_ds1302_min_add, &ds1302_buf[4]);
    DS1302_ReadByte(_ds1302_sec_add, &ds1302_buf[5]);
    DS1302_ReadByte(_ds1302_weekday_add, &ds1302_buf[6]);
}

void setNTPTime(unsigned char *time)
{
    ntp_time[0] = time[0];
    ntp_time[1] = time[1];
    ntp_time[2] = time[2];
    ntp_time[3] = time[3];
    ntp_time[4] = time[4];
    ntp_time[5] = time[5];
}

/*
@brief 获取DS1302时间
@details 传入7字节时间数组
@param 年 月 日 时 分 秒 星期
*/
void getDS1302Time(unsigned char *time)
{
    time[0] = ds1302_buf[0];
    time[1] = ds1302_buf[1];
    time[2] = ds1302_buf[2];
    time[3] = ds1302_buf[3];
    time[4] = ds1302_buf[4];
    time[5] = ds1302_buf[5];
    time[6] = ds1302_buf[6];
}
