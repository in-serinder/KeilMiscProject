#include "i2cm.h"

#if I2C_USE_MASTER

/* ==========================================================================
 * 软 I2C 主机实现
 * --------------------------------------------------------------------------
 * 时序约定：SCL 空闲为高；SDA 只在 SCL 低电平期间改变，SCL 高电平时必须稳定。
 *   START = SCL 高时 SDA 下降
 *   STOP  = SCL 高时 SDA 上升
 *   ACK   = 第 9 个时钟期间从机把 SDA 拉低
 *
 * 这一层完全不知道"从机是谁"，也完全不知道 UART 的存在。
 * 唯一的外部联系是 I2C_BUS_HOOK()：单机自测时它被定义成 I2CS_Poll()，
 * 实际多机时为空宏。
 * ========================================================================== */

static unsigned char s_err = I2C_OK;
static unsigned char s_loops = I2C_DELAY_LOOPS;

/* ---------------- 基础电平操作 ---------------- */

static void m_delay(void)
{
    unsigned char i = s_loops;
    while (i--)
        _nop_();
}

static void sda_hi(void)
{
    I2C_SDA = 1;
    m_delay();
    I2C_BUS_HOOK();
}

static void sda_lo(void)
{
    I2C_SDA = 0;
    m_delay();
    I2C_BUS_HOOK();
}

/* 释放 SCL 并等它真正变高。
   从机可以"拉伸时钟"——拉住 SCL 不放，要求主机等它。所以不能写完 1 就走，
   必须回读引脚。准双向口先写 1 才有资格读回真实电平。 */
static bit scl_hi(void)
{
    unsigned char t = I2C_STRETCH_TIMEOUT;

    I2C_SCL = 1;
    while (!I2C_SCL)
    {
        if (--t == 0)
        {
            s_err = I2C_ERR_BUSY;
            return 0;
        }
    }
    m_delay();
    I2C_BUS_HOOK();
    return 1;
}

static void scl_lo(void)
{
    I2C_SCL = 0;
    m_delay();
    I2C_BUS_HOOK();
}

/* ---------------- 初始化与总线恢复 ---------------- */

void I2CM_Init(void)
{
    unsigned char i;

    s_err = I2C_OK;

    /* 先把两根线都放开（写 1 = 释放），总线才是干净的 */
    I2C_SCL = 1;
    I2C_SDA = 1;
    m_delay();

    /* 总线恢复：如果上一次通信被断电/复位打断，某个从机可能正拉着 SDA 等时钟，
       于是 SDA 恒低、之后再发 START 也发不出去。补最多 9 个时钟让它把那个字节
       吐完，再补一个 STOP，总线就回来了。这是从"卡死"里自救的唯一办法。 */
    if (!I2C_SDA)
    {
        for (i = 0; i < 9 && !I2C_SDA; i++)
        {
            scl_lo();
            scl_hi();
        }
        I2CM_Stop();
    }
    I2C_BUS_HOOK();
}

void I2CM_SetSpeed(unsigned char loops)
{
    s_loops = loops;
}

unsigned char I2CM_Speed(void)
{
    return s_loops;
}

unsigned char I2CM_LastError(void)
{
    return s_err;
}

/* ---------------- 低层：字节级 ---------------- */

bit I2CM_Start(void)
{
    /* 发 START 之前总线必须是空闲的：SCL 高（含等从机放钟）、SDA 高 */
    if (!scl_hi())
        return 0;

    if (!I2C_SDA)
    {
        s_err = I2C_ERR_BUSY;   /* SDA 被某片拉住，总线忙 */
        scl_lo();
        return 0;
    }
    return I2CM_Restart();
}

bit I2CM_Restart(void)
{
    sda_hi();        /* SCL 低时把 SDA 抬起来 */
    if (!scl_hi())
        return 0;
    sda_lo();        /* SCL 高时 SDA 下降 = START / 重复起始 */
    scl_lo();
    return 1;
}

void I2CM_Stop(void)
{
    sda_lo();
    scl_hi();        /* SCL 高时 SDA 上升 = STOP */
    sda_hi();
    m_delay();
}

bit I2CM_WriteByte(unsigned char dat)
{
    unsigned char i;
    bit ack;

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80)
            sda_hi();
        else
            sda_lo();
        dat = (unsigned char)(dat << 1);
        if (!scl_hi())
            return 0;
        scl_lo();
    }

    sda_hi();        /* 释放 SDA，把第 9 个时钟让给从机应答 */
    if (!scl_hi())
        return 0;
    ack = (bit)(I2C_SDA == 0);
    scl_lo();

    if (!ack)
        s_err = I2C_ERR_NACK;
    return ack;
}

unsigned char I2CM_ReadByte(bit nack)
{
    unsigned char i, dat = 0;

    sda_hi();        /* 输入态：8051 准双向口必须先写 1 */
    for (i = 0; i < 8; i++)
    {
        dat = (unsigned char)(dat << 1);
        if (!scl_hi())
            return dat;
        if (I2C_SDA)
            dat |= 0x01;
        scl_lo();
    }

    /* 读完由主机发应答：还有后续字节就 ACK(拉低)，最后一个字节发 NACK */
    if (nack)
        sda_hi();
    else
        sda_lo();
    scl_hi();
    scl_lo();
    sda_hi();
    return dat;
}

/* ---------------- 高层：帧级 ---------------- */

bit I2CM_WriteFrame(unsigned char addr, unsigned char *buf, unsigned char len)
{
    unsigned char i;
    bit ok;

    s_err = I2C_OK;
    if (!I2CM_Start())
        return 0;

    ok = I2CM_WriteByte((unsigned char)(addr << 1));
    for (i = 0; ok && i < len; i++)
        ok = I2CM_WriteByte(buf[i]);

    I2CM_Stop();
    return ok;
}

bit I2CM_ReadFrame(unsigned char addr, unsigned char *buf, unsigned char len)
{
    unsigned char i;
    bit ok;

    s_err = I2C_OK;
    if (len == 0)
    {
        s_err = I2C_ERR_PARAM;
        return 0;
    }
    if (!I2CM_Start())
        return 0;

    ok = I2CM_WriteByte((unsigned char)((addr << 1) | 0x01));
    if (ok)
    {
        for (i = 0; i < len; i++)
            buf[i] = I2CM_ReadByte((bit)(i == (unsigned char)(len - 1)));
    }

    I2CM_Stop();
    return ok;
}

bit I2CM_WriteRead(unsigned char addr,
                   unsigned char *wbuf, unsigned char wlen,
                   unsigned char *rbuf, unsigned char rlen)
{
    unsigned char i;
    bit ok;

    s_err = I2C_OK;
    if (!I2CM_Start())
        return 0;

    ok = I2CM_WriteByte((unsigned char)(addr << 1));
    for (i = 0; ok && i < wlen; i++)
        ok = I2CM_WriteByte(wbuf[i]);

    if (ok && rlen)
    {
        ok = I2CM_Restart();
        if (ok)
            ok = I2CM_WriteByte((unsigned char)((addr << 1) | 0x01));
        for (i = 0; ok && i < rlen; i++)
            rbuf[i] = I2CM_ReadByte((bit)(i == (unsigned char)(rlen - 1)));
    }

    I2CM_Stop();
    return ok;
}

#if I2CM_USE_REG_HELPERS
bit I2CM_WriteReg(unsigned char addr, unsigned char reg, unsigned char dat)
{
    unsigned char b[2];

    b[0] = reg;
    b[1] = dat;
    return I2CM_WriteFrame(addr, b, 2);
}

bit I2CM_ReadReg(unsigned char addr, unsigned char reg, unsigned char *dat)
{
    return I2CM_WriteRead(addr, &reg, 1, dat, 1);
}
#endif

/* ---------------- 探测 / 扫描 ---------------- */

bit I2CM_Probe(unsigned char addr7)
{
    bit ok;

    s_err = I2C_OK;
    if (!I2CM_Start())
        return 0;

    ok = I2CM_WriteByte((unsigned char)(addr7 << 1));
    I2CM_Stop();

    if (!ok)
        s_err = I2C_OK;     /* 探测不到不是"故障"，是"没人" */
    return ok;
}

unsigned char I2CM_Scan(unsigned char *out, unsigned char max)
{
    unsigned char a, n = 0;

    if (max == 0)
        return 0;

    for (a = 0x08; a <= 0x77; a++)
    {
        if (I2CM_Probe(a))
        {
            out[n++] = a;
            if (n >= max)
                break;
        }
    }
    return n;
}

#endif /* I2C_USE_MASTER */
/* 本机只做从机时，本文件编译为空目标文件 —— Keil C51 不会为此产生任何告警，
   所以两种角色可以同时留在工程里，切角色只改 soft_i2c.h 里的开关。 */
