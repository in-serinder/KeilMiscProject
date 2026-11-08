#include "board.h"
/* i2c_hw.c — IIC1 硬件 I2C 主机驱动（STC8 硬件 I2C 外设）*/
#include "i2c_hw.h"
#include "delay.h"

uint8_t I2C1_Err = 0;

static void i2c1_wait(void)
{
    uint16_t guard = 60000U;        /* 死等保护：外设没使能时会卡死在 while */

    while (!(I2CMSST & 0x40))
    {
        if (--guard == 0)
        {
            break;
        }
    }
    I2CMSST &= (uint8_t)~0x40;      /* MSIF 软件清零 */
}

void I2C1_Recover(void)
{
    uint8_t i;

    /* 把 SCL/SDA 当普通 IO：准双向、释放总线 */
    P3M1 &= (uint8_t)~0x0C;
    P3M0 &= (uint8_t)~0x0C;
    P3PU |= 0x0C;
    P32 = 1;
    P33 = 1;
    delay_us(5);

    if (P33 == 0)                   /* SDA 被从机拉住才需要补时钟 */
    {
        for (i = 0; i < 9; i++)
        {
            P32 = 0;
            delay_us(5);
            P32 = 1;
            delay_us(5);
        }
        P33 = 0;                    /* 手工 STOP：SCL 高时 SDA 上升 */
        delay_us(5);
        P32 = 1;
        delay_us(5);
        P33 = 1;
        delay_us(5);
    }
}

void I2C1_SetPinSel(uint8_t sel)
{
    P_SW2 |= 0x80;                                                  /* EAXFR */
    P_SW2 = (uint8_t)((P_SW2 & (uint8_t)~0x30) |
                      (uint8_t)((sel & 0x03) << 4));
    I2CCFG  = (uint8_t)(0xC0 | (CFG_I2C_HW_MSSPEED & 0x3F));        /* ENI2C=1 MSSL=1 */
    I2CMSST = 0x00;
}

void I2C1_Init(void)
{
    I2C1_Recover();
    I2C1_SetPinSel(CFG_I2C_S_SEL & 0x03);
    I2C1_Err = 0;
}

void I2C1_Start(void)          { I2CMSCR = 0x01; i2c1_wait(); }
void I2C1_Stop(void)           { I2CMSCR = 0x06; i2c1_wait(); }
void I2C1_SendByte(uint8_t b)  { I2CTXD = b; I2CMSCR = 0x02; i2c1_wait(); }
uint8_t I2C1_RecvByte(void)    { I2CMSCR = 0x04; i2c1_wait(); return I2CRXD; }

bit I2C1_WaitAck(void)
{
    I2CMSCR = 0x03;                 /* RECV_ACK */
    i2c1_wait();
    return (bit)((I2CMSST & 0x02) ? 1 : 0);   /* MSACKI = 1 -> NACK */
}

void I2C1_SendAck(bit nack)
{
    I2CMSST = nack ? 0x01 : 0x00;   /* MSACKO: 1=NACK 0=ACK */
    I2CMSCR = 0x05;
    i2c1_wait();
}

bit I2C1_Write(uint8_t addr7, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    I2C1_Err = 0;
    I2C1_Start();
    I2C1_SendByte((uint8_t)(addr7 << 1));
    if (I2C1_WaitAck())
    {
        I2C1_Err = 1;
        I2C1_Stop();
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        I2C1_SendByte(buf[i]);
        if (I2C1_WaitAck())
        {
            I2C1_Err = 4;
            I2C1_Stop();
            return 0;
        }
    }

    I2C1_Stop();
    return 1;
}

bit I2C1_WriteRead(uint8_t addr7, const uint8_t *wbuf, uint8_t wn,
                   uint8_t *rbuf, uint8_t rn)
{
    uint8_t i;

    I2C1_Err = 0;
    I2C1_Start();
    I2C1_SendByte((uint8_t)(addr7 << 1));
    if (I2C1_WaitAck())
    {
        I2C1_Err = 1;
        I2C1_Stop();
        return 0;
    }

    for (i = 0; i < wn; i++)
    {
        I2C1_SendByte(wbuf[i]);
        if (I2C1_WaitAck())
        {
            I2C1_Err = (uint8_t)(2 + (i & 1));
            I2C1_Stop();
            return 0;
        }
    }

    I2C1_Start();                   /* 重复 START */
    I2C1_SendByte((uint8_t)((addr7 << 1) | 0x01));
    if (I2C1_WaitAck())
    {
        I2C1_Err = 5;
        I2C1_Stop();
        return 0;
    }

    for (i = 0; i < rn; i++)
    {
        rbuf[i] = I2C1_RecvByte();
        I2C1_SendAck((bit)(i == (uint8_t)(rn - 1)));   /* 最后一字节 NACK */
    }

    I2C1_Stop();
    return 1;
}

bit I2C1_Probe(uint8_t addr7)
{
    I2C1_Err = 0;
    I2C1_Start();
    I2C1_SendByte((uint8_t)(addr7 << 1));
    if (I2C1_WaitAck())
    {
        I2C1_Err = 1;
        I2C1_Stop();
        return 0;
    }
    I2C1_Stop();
    return 1;
}

/* 空闲时采样 SCL/SDA 电平（bit0 = SCL/P3.2，bit1 = SDA/P3.3）。
 * 正常总线空闲时必须两根都为高（上拉把它拉起来）。读回 0 说明该线被拉死 /
 * 没上拉 / 器件没供电 —— 这时所有地址都会被误判成 ACK（SDA 恒低），
 * 扫描位图和读到的数据全是假的。*/
uint8_t I2C1_BusLevels(void)
{
    uint8_t cfg;
    uint8_t v;

    I2C1_Err = 0;
    cfg = I2CCFG;
    I2CCFG = 0x00;                  /* 关 ENI2C，引脚交回 GPIO */

    P3M1 &= (uint8_t)~0x0C;         /* 准双向 */
    P3M0 &= (uint8_t)~0x0C;
    P3PU |= 0x0C;                   /* 内部上拉，尽量把线拉起来 */
    P32 = 1;
    P33 = 1;
    delay_us(20);                   /* 等上拉把线拉稳 */

    v = 0;
    if (P32)
    {
        v |= 0x01;
    }
    if (P33)
    {
        v |= 0x02;
    }

    I2CCFG = cfg;                   /* 恢复 I2C 外设 */
    return v;
}
