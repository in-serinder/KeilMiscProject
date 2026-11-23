/* =====================================================================
 * eeprom.c  —  用 STC8 片内 IAP(Flash) 存参数
 *
 * 寄存器（STC8 系列，和 STC15 不同，这里没有 WT[3:0] 等待周期字段，
 * 改由 IAP_TPS = SYSclk / 1MHz 告诉硬件主频）：
 *   IAP_DATA  读写数据
 *   IAP_ADDRH/L  目标地址（16 位）
 *   IAP_CMD   0=空 1=读 2=写(字节) 3=擦除(扇区)
 *   IAP_TRIG  依次写 0x5A、0xA5 才真正执行（防误操作）
 *   IAP_CONTR bit7 = IAPEN
 *   IAP_TPS   22.1184MHz -> 22
 *
 * 关键点：
 * 1) STC8G1K08A 扇区 512B，擦除只能整扇区擦，所以先擦后写。
 * 2) 擦/写期间 CPU 会被硬件停住，节拍会丢几十个 ms —— 关中断再把 EA 恢复，
 *    避免中断向量在 Flash 忙于编程时取指出问题。
 * 3) 操作完必须把 IAP_CONTR 关掉、IAP_CMD 清零、地址指到 0x8000（非用户区），
 *    否则程序跑飞时可能误触发一次擦写。
 * 4) STC-ISP 下载时"EEPROM 大小"要与 CFG_EEPROM_BASE 对齐（8K 器件选 1K，
 *    即 0x1C00~0x1FFF），否则下载代码可能覆盖参数区。
 * ===================================================================== */
#include "eeprom.h"
#include "delay.h"

#define IAP_CMD_IDLE            0x00
#define IAP_CMD_READ            0x01
#define IAP_CMD_WRITE           0x02
#define IAP_CMD_ERASE           0x03

#define EE_REC_LEN              8

static uint8_t s_fail;      /* 最近一次 IAP 操作的 CMD_FAIL 位（1 = 硬件报告失败） */

static void iap_begin(uint8_t cmd, uint16_t addr)
{
    IAP_TPS = (uint8_t)(FOSC_HZ / 1000000UL);   /* 22.1184MHz -> 22 */
    IAP_CONTR = 0x80;                           /* IAPEN = 1，程序区可写 */
    IAP_CMD = cmd;
    IAP_ADDRH = (uint8_t)(addr >> 8);
    IAP_ADDRL = (uint8_t)(addr & 0x00FF);
}

static void iap_trigger(void)
{
    IAP_TRIG = 0x5A;
    IAP_TRIG = 0xA5;
    _nop_();
    _nop_();
    _nop_();
    _nop_();
}

static void iap_end(void)
{
    s_fail = (uint8_t)((IAP_CONTR & 0x10) ? 1 : 0);   /* bit4 = CMD_FAIL */
    IAP_CONTR = 0x00;
    IAP_CMD = IAP_CMD_IDLE;
    IAP_ADDRH = 0x80;                           /* 指向非用户区，防误触发 */
    IAP_ADDRL = 0x00;
    IAP_TRIG = 0x00;
}

static uint8_t ee_read_byte(uint16_t addr)
{
    uint8_t d;

    iap_begin(IAP_CMD_READ, addr);
    iap_trigger();
    d = IAP_DATA;
    iap_end();
    return d;
}

static void ee_write_byte(uint16_t addr, uint8_t dat)
{
    iap_begin(IAP_CMD_WRITE, addr);
    IAP_DATA = dat;
    iap_trigger();
    delay_ms(2);                                /* 单字节编程，留足余量 */
    iap_end();
}

static void ee_erase_sector(uint16_t addr)
{
    iap_begin(IAP_CMD_ERASE, addr);
    iap_trigger();
    delay_ms(6);                                /* 扇区擦除典型 4~5ms */
    iap_end();
}

#if CFG_DEBUG_UART
uint8_t EE_LastFail(void)
{
    return s_fail;
}
#endif

uint8_t EE_LoadThreshold(uint16_t *th)
{
    uint8_t rec[EE_REC_LEN];
    uint8_t i;
    uint8_t sum = 0;
    uint16_t v, nv;

    for (i = 0; i < EE_REC_LEN; i++)
    {
        rec[i] = ee_read_byte((uint16_t)(CFG_EEPROM_BASE + i));
    }

    if ((rec[0] != 0x5A) || (rec[1] != 0xA5))
    {
        return 0;
    }

    for (i = 0; i < 6; i++)
    {
        sum = (uint8_t)(sum + rec[i]);
    }
    if (sum != rec[6])
    {
        return 0;
    }

    v  = (uint16_t)((uint16_t)rec[2] | ((uint16_t)rec[3] << 8));
    nv = (uint16_t)((uint16_t)rec[4] | ((uint16_t)rec[5] << 8));

    if ((uint16_t)(~v) != nv)
    {
        return 0;
    }
    if (v > CFG_TH_MAX)
    {
        return 0;
    }

    *th = v;
    return 1;
}

uint8_t EE_SaveThreshold(uint16_t th)
{
    uint8_t rec[EE_REC_LEN];
    uint8_t i;
    uint8_t ok = 1;
    uint16_t nv;
    bit ea;

    nv = (uint16_t)(~th);

    rec[0] = 0x5A;
    rec[1] = 0xA5;
    rec[2] = (uint8_t)(th & 0x00FF);
    rec[3] = (uint8_t)(th >> 8);
    rec[4] = (uint8_t)(nv & 0x00FF);
    rec[5] = (uint8_t)(nv >> 8);
    rec[6] = 0x00;
    for (i = 0; i < 6; i++)
    {
        rec[6] = (uint8_t)(rec[6] + rec[i]);
    }
    rec[7] = 0xFF;

    ea = EA;
    EA = 0;                                     /* IAP 期间不让中断插进来 */

    ee_erase_sector(CFG_EEPROM_BASE);
    for (i = 0; i < EE_REC_LEN; i++)
    {
        ee_write_byte((uint16_t)(CFG_EEPROM_BASE + i), rec[i]);
    }

    EA = ea;

    for (i = 0; i < EE_REC_LEN; i++)
    {
        if (ee_read_byte((uint16_t)(CFG_EEPROM_BASE + i)) != rec[i])
        {
            ok = 0;
            break;
        }
    }

    return ok;
}
