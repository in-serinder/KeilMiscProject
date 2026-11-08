#include "board.h"
/* i2c_sw.c — IIC2 软件 I2C 主机
 *
 * 引脚可在 P5.4/P5.5 与 P1.4/P1.5 之间**运行时选择**：
 *   P5.4 出厂是复位脚，即使 ISP 里勾了"用作 I/O"，个别批次的丝印/焊接也可能
 *   把 IIC2 落在 P1 组上。上电先测 P5 能不能推挽拉低，不能就自动改用 P1.4/P1.5，
 *   避免"整条总线一个器件都不应答"却查不出原因。
 *
 * 电平方式：准双向（写 1 = 弱上拉高，写 0 = 强下拉）。
 * 不用开漏是因为开漏的"高"完全依赖上拉，而内部上拉在 P5PU（xdata 扩展 SFR），
 * 一旦没生效写 1 就成了悬空，整条总线永远上不去。
 */
#include "i2c_sw.h"
#include "delay.h"

uint8_t I2C2_Err = 0;

#define SW_DLY()        delay_us(CFG_SW_I2C_DELAY)

/* 0 = P5.4/P5.5，1 = P1.4/P1.5（运行时决定，见 I2C2_Init）*/
static bit s_p1 = 0;

/* ---------------------------------------------------------------------
 * 引脚层
 * ------------------------------------------------------------------- */
static void pin_cfg(void)
{
    if (s_p1)
    {
        P1M1 &= (uint8_t)~0x30;     /* 准双向 */
        P1M0 &= (uint8_t)~0x30;
        P14 = 1;
        P15 = 1;
    }
    else
    {
        P5M1 &= (uint8_t)~0x30;
        P5M0 &= (uint8_t)~0x30;
        P5PU |= 0x30;               /* 内部上拉做余量，不依赖它 */
        P54 = 1;
        P55 = 1;
    }
    SW_DLY();
}

static void scl_high(void) { if (s_p1) { P14 = 1; } else { P54 = 1; } SW_DLY(); }
static void scl_low(void)  { if (s_p1) { P14 = 0; } else { P54 = 0; } SW_DLY(); }
static void sda_high(void) { if (s_p1) { P15 = 1; } else { P55 = 1; } SW_DLY(); }
static void sda_low(void)  { if (s_p1) { P15 = 0; } else { P55 = 0; } SW_DLY(); }

static bit sda_level(void)
{
    return (bit)(s_p1 ? (bit)P15 : (bit)P55);
}

/* 推挽拉低再读回：1 = 这两个脚真受 CPU 控制 */
static bit pins_drive_test(void)
{
    bit ok;

    if (s_p1)
    {
        P1M1 &= (uint8_t)~0x30;
        P1M0 |= 0x30;
        P14 = 0;
        P15 = 0;
        SW_DLY();
        ok = (bit)((P14 == 0) && (P15 == 0));
        P14 = 1;
        P15 = 1;
        P1M1 = 0xFF;                /* 不用时恢复高阻输入 */
        P1M0 = 0x00;
    }
    else
    {
        P5M1 &= (uint8_t)~0x30;
        P5M0 |= 0x30;
        P54 = 0;
        P55 = 0;
        SW_DLY();
        ok = (bit)((P54 == 0) && (P55 == 0));
        P54 = 1;
        P55 = 1;
    }

    pin_cfg();
    return ok;
}

/* 当前引脚组上是否有任何已知器件应答 */
static bit bus_alive(void)
{
    if (I2C2_Probe(ADDR_OLED))
    {
        return 1;
    }
    if (I2C2_Probe((uint8_t)(ADDR_OLED + 1)))
    {
        return 1;
    }
    if (I2C2_Probe(ADDR_PCF8574))
    {
        return 1;
    }
    if (I2C2_Probe(ADDR_META_24C02))
    {
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * 初始化：选引脚组 -> 配准双向 -> 总线恢复
 *
 * 选组规则（避免静默选错总线）：
 *   1. 优先用 P5.4/P5.5：能推挽拉低、且总线上有器件应答；
 *   2. 否则试 P1.4/P1.5：**必须真有器件应答**才切过去；
 *   3. 都不满足则留在 P5，如实报告引脚状态，交给 DIAG/PINS 去查。
 * ------------------------------------------------------------------- */
void I2C2_Init(void)
{
    s_p1 = 0;
    pin_cfg();

    if (!pins_drive_test() || !bus_alive())
    {
        s_p1 = 1;
        pin_cfg();

        if (!bus_alive())
        {
            s_p1 = 0;               /* P1 上也没东西：回默认，别乱切 */
            pin_cfg();
        }
    }

    I2C2_Err = 0;
    I2C2_Recover();                 /* 从机若在上电瞬间拉住 SDA，先补时钟解开 */
}

/* 1 = 当前引脚组真受控 */
bit I2C2_PinsOk(void)
{
    return pins_drive_test();
}

/* 把当前引脚组恢复成准双向并释放总线（诊断命令动过引脚后调用）*/
void I2C2_RestorePins(void)
{
    pin_cfg();
}

/* 当前使用的引脚组：0 = P5.4/P5.5，1 = P1.4/P1.5 */
uint8_t I2C2_PinPair(void)
{
    return s_p1 ? 1 : 0;
}

void I2C2_Recover(void)
{
    uint8_t i;

    scl_high();
    sda_high();

    if (sda_level() == 0)           /* 从机拉住 SDA：补 9 个时钟 */
    {
        for (i = 0; i < 9; i++)
        {
            scl_low();
            scl_high();
        }
        sda_low();
        SW_DLY();
        scl_high();
        sda_high();
    }
}

void I2C2_Start(void)
{
    sda_high();
    scl_high();
    sda_low();
    scl_low();
}

void I2C2_Stop(void)
{
    sda_low();
    scl_high();
    sda_high();
    SW_DLY();
}

bit I2C2_SendByte(uint8_t b)
{
    uint8_t i;
    bit ack;

    for (i = 0; i < 8; i++)
    {
        if (b & 0x80)
        {
            sda_high();
        }
        else
        {
            sda_low();
        }
        b = (uint8_t)(b << 1);
        scl_high();
        scl_low();
    }

    sda_high();                     /* 释放 SDA，读从机 ACK */
    SW_DLY();
    scl_high();
    ack = (bit)(sda_level() == 0);  /* 0 = ACK */
    scl_low();
    sda_high();

    return ack;
}

uint8_t I2C2_RecvByte(bit nack)
{
    uint8_t i;
    uint8_t v = 0;

    sda_high();
    for (i = 0; i < 8; i++)
    {
        v = (uint8_t)(v << 1);
        scl_high();
        if (sda_level())
        {
            v |= 0x01;
        }
        scl_low();
    }

    if (nack)
    {
        sda_high();                 /* NACK：留给从机释放 */
    }
    else
    {
        sda_low();                  /* ACK */
    }
    scl_high();
    scl_low();
    sda_high();

    return v;
}

bit I2C2_Write(uint8_t addr7, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    I2C2_Err = 0;
    I2C2_Start();
    if (!I2C2_SendByte((uint8_t)(addr7 << 1)))
    {
        I2C2_Err = 1;
        I2C2_Stop();
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        if (!I2C2_SendByte(buf[i]))
        {
            I2C2_Err = 4;
            I2C2_Stop();
            return 0;
        }
    }

    I2C2_Stop();
    return 1;
}

bit I2C2_WriteRead(uint8_t addr7, const uint8_t *wbuf, uint8_t wn,
                   uint8_t *rbuf, uint8_t rn)
{
    uint8_t i;

    I2C2_Err = 0;
    I2C2_Start();
    if (!I2C2_SendByte((uint8_t)(addr7 << 1)))
    {
        I2C2_Err = 1;
        I2C2_Stop();
        return 0;
    }

    for (i = 0; i < wn; i++)
    {
        if (!I2C2_SendByte(wbuf[i]))
        {
            I2C2_Err = 2;
            I2C2_Stop();
            return 0;
        }
    }

    I2C2_Start();
    if (!I2C2_SendByte((uint8_t)((addr7 << 1) | 0x01)))
    {
        I2C2_Err = 5;
        I2C2_Stop();
        return 0;
    }

    for (i = 0; i < rn; i++)
    {
        rbuf[i] = I2C2_RecvByte((bit)(i == (uint8_t)(rn - 1)));
    }

    I2C2_Stop();
    return 1;
}

bit I2C2_Probe(uint8_t addr7)
{
    bit ok;

    I2C2_Err = 0;
    I2C2_Start();
    ok = I2C2_SendByte((uint8_t)(addr7 << 1));
    I2C2_Stop();
    if (!ok)
    {
        I2C2_Err = 1;
    }
    return ok;
}
