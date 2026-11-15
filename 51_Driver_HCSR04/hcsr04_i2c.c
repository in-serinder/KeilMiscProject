#include "hcsr04.h"

/* ==========================================================================
 * HC-SR04 驱动 —— 模式 1：I2C
 * --------------------------------------------------------------------------
 * 模块拨码：M1 = 1，M2 = 0
 * P20 = SCL，P21 = SDA
 *
 * 注意：这是**软件 I2C**。STC89C52 的硬件 I2C 引脚不在 P2 上，
 *       P2.0/P2.1 没有 I2C 复用，只能位操作模拟。
 *
 * 地址和寄存器地址各厂家不一样，先用 HCSR04_I2C_Scan() 扫总线确认，
 * 再把结果填进 hcsr04.h 的 HCSR04_I2C_ADDR / HCSR04_I2C_REG。
 *
 * 只有 HCSR04_MODE == HCSR04_MODE_I2C 时本文件才产生代码。
 * ========================================================================== */

#if HCSR04_MODE == HCSR04_MODE_I2C

static unsigned int s_mm;
static unsigned int s_raw;
static unsigned char s_err;

unsigned int HCSR04_MM(void)
{
    return s_mm;
}

unsigned int HCSR04_CM(void)
{
    return (unsigned int)((s_mm + 5) / 10);
}

unsigned char HCSR04_Error(void)
{
    return s_err;
}

unsigned int HCSR04_Raw(void)
{
    return s_raw;
}

/* ---------- 软件 I2C 原语 ---------- */

static void i2c_delay(void)
{
    unsigned char i = 20;
    while (i--)
        ;
}

static void i2c_start(void)
{
    HCSR04_SDA = 1;
    HCSR04_SCL = 1;
    i2c_delay();
    HCSR04_SDA = 0;
    i2c_delay();
    HCSR04_SCL = 0;
}

static void i2c_stop(void)
{
    HCSR04_SDA = 0;
    i2c_delay();
    HCSR04_SCL = 1;
    i2c_delay();
    HCSR04_SDA = 1;
    i2c_delay();
}

/* 发一个字节；返回 1 = 收到应答 */
static bit i2c_write(unsigned char dat)
{
    unsigned char i;
    bit nack;

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80)
            HCSR04_SDA = 1;
        else
            HCSR04_SDA = 0;
        dat <<= 1;
        i2c_delay();
        HCSR04_SCL = 1;
        i2c_delay();
        HCSR04_SCL = 0;
    }

    HCSR04_SDA = 1; /* 释放，让从机应答 */
    i2c_delay();
    HCSR04_SCL = 1;
    i2c_delay();
    nack = HCSR04_SDA; /* 0 = 应答 */
    HCSR04_SCL = 0;
    i2c_delay();
    return (bit)(nack == 0);
}

/* 读一个字节；nack = 1 表示读完发 NACK（最后一个字节） */
static unsigned char i2c_read(bit nack)
{
    unsigned char i, dat = 0;

    HCSR04_SDA = 1; /* 输入 */
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        HCSR04_SCL = 1;
        i2c_delay();
        if (HCSR04_SDA)
            dat |= 0x01;
        HCSR04_SCL = 0;
        i2c_delay();
    }

    if (nack)
        HCSR04_SDA = 1;
    else
        HCSR04_SDA = 0;
    i2c_delay();
    HCSR04_SCL = 1;
    i2c_delay();
    HCSR04_SCL = 0;
    i2c_delay();
    HCSR04_SDA = 1;
    return dat;
}

/* ---------- 对外 API ---------- */

/* 扫描 0x08~0x77，把有应答的 7 位地址存进 buf，返回个数 */
unsigned char HCSR04_I2C_Scan(unsigned char *buf, unsigned char max)
{
    unsigned char a, n = 0;

    for (a = 0x08; a <= 0x77; a++)
    {
        i2c_start();
        if (i2c_write((unsigned char)(a << 1)))
        {
            if (n < max)
                buf[n++] = a;
        }
        i2c_stop();
    }
    return n;
}

/* 先写寄存器地址，再读 2 字节 */
bit HCSR04_I2C_Read2(unsigned char reg, unsigned char *buf)
{
    i2c_start();
    if (!i2c_write(HCSR04_I2C_ADDR))
    {
        i2c_stop();
        s_err = HCSR04_ERR_NACK;
        return 0;
    }
    if (!i2c_write(reg))
    {
        i2c_stop();
        s_err = HCSR04_ERR_NACK;
        return 0;
    }
    i2c_start();
    if (!i2c_write((unsigned char)(HCSR04_I2C_ADDR | 0x01)))
    {
        i2c_stop();
        s_err = HCSR04_ERR_NACK;
        return 0;
    }
    buf[0] = i2c_read(0);
    buf[1] = i2c_read(1);
    i2c_stop();
    return 1;
}

void HCSR04_Init(void)
{
    HCSR04_SCL = 1;
    HCSR04_SDA = 1;
    s_mm = 0;
    s_raw = 0;
    s_err = HCSR04_OK;
}

bit HCSR04_Measure(void)
{
    unsigned char buf[2];

    s_err = HCSR04_OK;
    s_mm = 0;
    s_raw = 0;

    if (!HCSR04_I2C_Read2(HCSR04_I2C_REG, buf))
        return 0;

    /* 高字节在前。若实测发现单位是 cm 或 0.1cm，改下面两行即可 */
    s_raw = ((unsigned int)buf[0] << 8) | buf[1];
    s_mm = s_raw;
    return 1;
}

#endif /* HCSR04_MODE_I2C */
