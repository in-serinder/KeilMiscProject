#include "board.h"
/* ee.c — 24Cxx 访问实现（阵列走 IIC1 硬件 I2C，元数据 24C02 走 IIC2 软 I2C）*/
#include "ee.h"
#include "i2c_hw.h"
#include "i2c_sw.h"
#include "delay.h"

/* 逐型号参数表（code 常量，Kbit 由 EE_Model 映射）*/
static const ee_model_t code EE_M32   = { 0x00FFFUL,  32,   4096UL, 2 };
static const ee_model_t code EE_M64   = { 0x01FFFUL,  32,   8192UL, 2 };
static const ee_model_t code EE_M128  = { 0x03FFFUL,  64,  16384UL, 2 };
static const ee_model_t code EE_M256  = { 0x07FFFUL,  64,  32768UL, 2 };
static const ee_model_t code EE_M512  = { 0x0FFFFUL, 128,  65536UL, 2 };
static const ee_model_t code EE_M1024 = { 0x1FFFFUL, 256, 131072UL, 2 };

static uint16_t s_model = EE_MODEL_24C64;
static const ee_model_t code *s_cur = &EE_M64;

const ee_model_t code *EE_Model(uint16_t kbit)
{
    switch (kbit)
    {
    case EE_MODEL_24C32:   return &EE_M32;
    case EE_MODEL_24C128:  return &EE_M128;
    case EE_MODEL_24C256:  return &EE_M256;
    case EE_MODEL_24C512:  return &EE_M512;
    case EE_MODEL_24C1024: return &EE_M1024;
    case EE_MODEL_24C64:
    default:               return &EE_M64;
    }
}

void EE_InitArray(void)
{
    I2C1_Init();
    s_model = EE_MODEL_24C64;
    s_cur = &EE_M64;
}

void EE_SetModel(uint16_t kbit)
{
    s_model = kbit;
    s_cur = EE_Model(kbit);
}

uint16_t EE_GetModel(void)
{
    return s_model;
}

uint32_t EE_MaxAddr(void)
{
    return s_cur->max_addr;
}

uint32_t EE_ChipBytes(void)
{
    return s_cur->cap_bytes;
}

/* ---------------------------------------------------------------------
 * 写周期等待（策略由 CFG_EE_ACK_POLL 选择，见 board.h）
 *
 * ACK polling：写事务结束后，芯片内部进入写周期（tWR），期间对器件地址
 * 一律不回 ACK；写完立即恢复应答。所以"轮询到 ACK"就是写完成的准确信号，
 * 典型 1~2 ms 返回（5 ms 只是规格最坏值）。每次轮询是 START + 地址字节 +
 * STOP，自身约 0.1 ms，逐次间隔 1 ms。
 * ------------------------------------------------------------------- */
static void ee_wait_write(uint8_t sa)
{
#if CFG_EE_ACK_POLL
    uint16_t tmo = (uint16_t)(CFG_EE_ACK_POLL_TMO_MS * 2U);   /* 每轮约 0.5ms */

    while (tmo != 0)
    {
        tmo--;

        I2C1_Err = 0;
        I2C1_Start();
        I2C1_SendByte((uint8_t)(sa & 0xFE));      /* 写方向 */
        if (!I2C1_WaitAck())
        {
            I2C1_Stop();
            return;                               /* ACK = 写周期结束 */
        }
        I2C1_Stop();
        delay_ms(1);
    }

    /* 超时：芯片可能在写周期内错误 ACK，退回固定延时兜底 */
    delay_ms(CFG_EE_WR_WAIT_MS);
#else
    delay_ms(CFG_EE_WR_WAIT_MS);    /* 24Cxx tWR ≤ 5ms */
#endif
}

/* 器件地址 = 0x50 + 片号（24C32 以上 2 字节地址，高位不进器件地址）*/
static uint8_t ee_sa(uint8_t chipIdx)
{
    return (uint8_t)(ADDR_ARRAY_BASE + (chipIdx & 0x07));
}

/* 写事务的组装缓冲：片内地址（最多 2 B）+ 一包数据（CFG_PKT_BYTES）。
 * 以前这里是 `uint8_t tmp[4]`，写 16 字节时越界 14 字节，把栈上的 done/i/型号
 * 指针全冲掉 —— 现象是"写返回成功、回读全 0"（数据写到错误地址去了）。
 * C51 SMALL 模式局部数组在 128 B 的 data 区，必须放 xdata 静态区。*/
static uint8_t xdata s_ew[CFG_PKT_BYTES + 2];

bit EE_WriteBlock(uint8_t chipIdx, uint32_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t n;
    uint8_t done;
    const ee_model_t code *m = s_cur;

    if (chipIdx > 7 || len == 0 || addr > m->max_addr)
    {
        return 0;
    }
    if ((uint32_t)len > (m->max_addr - addr + 1UL))
    {
        len = (uint8_t)(m->max_addr - addr + 1UL);
    }

    done = 0;
    while (done < len)
    {
        uint16_t room = (uint16_t)(m->page - (uint16_t)((addr + done) & (m->page - 1)));
        uint8_t chunk = (uint8_t)(len - done);
        uint8_t i;

        if (chunk > room)
        {
            chunk = (uint8_t)room;
        }
        if (chunk > CFG_PKT_BYTES)  /* 缓冲上限（正常不会触发，防御）*/
        {
            chunk = CFG_PKT_BYTES;
        }
        if (chunk == 0)
        {
            return 0;               /* 兜底：page 配置错误时不死循环 */
        }

        n = 0;
        if (m->addr_bytes == 2)
        {
            s_ew[n++] = (uint8_t)((addr + done) >> 8);
        }
        s_ew[n++] = (uint8_t)((addr + done) & 0xFF);
        for (i = 0; i < chunk; i++)
        {
            s_ew[n++] = buf[done + i];
        }

        if (!I2C1_Write(ee_sa(chipIdx), s_ew, n))
        {
            return 0;
        }

        ee_wait_write(ee_sa(chipIdx));
        done = (uint8_t)(done + chunk);
    }

    return 1;
}

bit EE_ReadBlock(uint8_t chipIdx, uint32_t addr, uint8_t *buf, uint8_t len)
{
    uint8_t tmp[2];
    uint8_t n = 0;
    const ee_model_t code *m = s_cur;

    if (chipIdx > 7 || len == 0 || addr > m->max_addr)
    {
        return 0;
    }
    if ((uint32_t)len > (m->max_addr - addr + 1UL))
    {
        len = (uint8_t)(m->max_addr - addr + 1UL);
    }

    if (m->addr_bytes == 2)
    {
        tmp[n++] = (uint8_t)(addr >> 8);
    }
    tmp[n++] = (uint8_t)(addr & 0xFF);

    return I2C1_WriteRead(ee_sa(chipIdx), tmp, n, buf, len);
}

bit EE_Probe(uint8_t chipIdx)
{
    if (chipIdx > 7)
    {
        return 0;
    }
    return I2C1_Probe(ee_sa(chipIdx));
}

uint8_t EE_ScanMask(void)
{
    uint8_t i;
    uint8_t mask = 0;

    for (i = 0; i < 8; i++)
    {
        if (EE_Probe(i))
        {
            mask |= (uint8_t)(1U << i);
        }
    }
    return mask;
}

/* ---------------------------------------------------------------------
 * 24C02（IIC2 软件总线，1 字节片内地址，页 8 B）
 * ------------------------------------------------------------------- */
void M24_Init(void)
{
    I2C2_Init();
}

bit M24_Read(uint8_t addr, uint8_t *buf, uint8_t len)
{
    if (len == 0 || (uint16_t)addr + len > 256U)
    {
        return 0;
    }
    return I2C2_WriteRead(ADDR_META_24C02, &addr, 1, buf, len);
}

bit M24_Write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t tmp[10];
    uint8_t done = 0;

    if (len == 0 || (uint16_t)addr + len > 256U)
    {
        return 0;
    }

    while (done < len)
    {
        /* 24C02 页 8 B，跨页会回卷，必须按页拆 */
        uint8_t room = (uint8_t)(8U - (uint8_t)((addr + done) & 0x07U));
        uint8_t chunk = (uint8_t)(len - done);
        uint8_t i;

        if (chunk > room)
        {
            chunk = room;
        }

        tmp[0] = (uint8_t)(addr + done);
        for (i = 0; i < chunk; i++)
        {
            tmp[1 + i] = buf[done + i];
        }

        if (!I2C2_Write(ADDR_META_24C02, tmp, (uint8_t)(chunk + 1)))
        {
            return 0;
        }

        delay_ms(5);                /* 24C02 tWR ≤ 10ms，取 5ms 实测足够 */
        done = (uint8_t)(done + chunk);
    }

    return 1;
}
