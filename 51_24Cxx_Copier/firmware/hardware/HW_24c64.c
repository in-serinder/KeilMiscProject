/* =====================================================================
 * HW_24c64.c — STC8G 硬件 I2C + 24Cxx（24C01~24C1024）驱动
 *
 * I2C 主机寄存器要点（STC8G 手册）：
 *   I2CCFG  (xdata 0xFE80): B7=ENI2C  B6=MSSL  B5..B0=MSSPEED
 *                           f_SCL = FOSC / 2 / (MSSPEED*2 + 4)
 *   I2CMSCR (xdata 0xFE81): B3..B0 = MSCMD  1=START 2=SEND 3=RECV_ACK
 *                           4=RECV 5=SEND_ACK 6=STOP
 *   I2CMSST (xdata 0xFE82): B6=MSIF(命令完成) B1=MSACKI(1=NACK) B0=MSACKO(1=NACK)
 *   P_SW2   (0xBA):         B7=EAXFR  B5:4=I2C_S 引脚组
 *
 * 【槽位与型号分开配置】
 *   之前按"地址 -> 型号"查表（照抄模拟器的测试表：0x50=24C01），
 *   结果板上 0x50 插 24C64 时，EDGE_0x00FF 会被判成越界 -> R-ERR。
 *   现在：地址只决定从机地址，型号由 EE_SLOT_TYPE[8] 决定，
 *   默认 8 个槽位都是 24C64。混装把对应项改成 EE_CHIP_xxx 即可。
 * ===================================================================== */
#include "HW_24c64.h"
#include "delay.h"

uint8_t I2C_Err = 0;

/* ---------------------------------------------------------------------
 * 型号库：索引 = EE_CHIP_xxx
 *   max_addr / 片内地址字节 / 页大小 / 页位数(1字节地址) / A16 掩码
 * ------------------------------------------------------------------- */
code ee_dev_t EE_TYPES[EE_CHIP_COUNT] =
{
    { 0x0007FUL, 1,   8, 0, 0 },                    /* 24C01    128B  */
    { 0x000FFUL, 1,   8, 0, 0 },                    /* 24C02    256B  */
    { 0x001FFUL, 1,  16, 1, 0 },                    /* 24C04    512B  */
    { 0x003FFUL, 1,  16, 2, 0 },                    /* 24C08    1KB   */
    { 0x007FFUL, 1,  16, 3, 0 },                    /* 24C16    2KB   */
    { 0x00FFFUL, 2,  32, 0, 0 },                    /* 24C32    4KB   */
    { 0x01FFFUL, 2,  32, 0, 0 },                    /* 24C64    8KB   */
    { 0x03FFFUL, 2,  64, 0, 0 },                    /* 24C128   16KB  */
    { 0x07FFFUL, 2,  64, 0, 0 },                    /* 24C256   32KB  */
    { 0x0FFFFUL, 2, 128, 0, 0 },                    /* 24C512   64KB  */
    { 0x1FFFFUL, 2, 256, 0, EE_1024_A16_MASK }      /* 24C1024  128KB */
};

/* ---------------------------------------------------------------------
 * 槽位 -> 型号：索引 = I2C 地址 - 0x50
 *
 * 默认全部是 EE_SLOT_CHIP_DEFAULT = 24C1024，含义是"2 字节地址、不限容量"，
 * 所以 24C32/64/128/256/512/1024 插哪个槽都能直接读写（EDGE 上限 0x1FFFF）。
 *
 * 只有片内地址 1 字节的 24C01/02/04/08/16 必须显式配置，例如只把 0x50 槽
 * 换成 24C16：
 *   { EE_CHIP_24C16, EE_SLOT_CHIP_DEFAULT, EE_SLOT_CHIP_DEFAULT, ... }
 * 配错成 1 字节器件会给 2 字节地址器件的片子多送一个地址字节，读写全错。
 * ------------------------------------------------------------------- */
code uint8_t EE_SLOT_TYPE[8] =
{
    EE_SLOT_CHIP_DEFAULT, EE_SLOT_CHIP_DEFAULT,
    EE_SLOT_CHIP_DEFAULT, EE_SLOT_CHIP_DEFAULT,
    EE_SLOT_CHIP_DEFAULT, EE_SLOT_CHIP_DEFAULT,
    EE_SLOT_CHIP_DEFAULT, EE_SLOT_CHIP_DEFAULT
};

static uint8_t s_addr7 = 0x50;  /* 当前槽位的 7 位从机地址 */
static uint8_t s_type  = EE_SLOT_CHIP_DEFAULT;
static bit     s_valid = 0;

/* ---------------------------------------------------------------------
 * I2C 基本动作
 * ------------------------------------------------------------------- */
static void i2c_wait(void)
{
    while (!(I2CMSST & 0x40));      /* 等 MSIF（命令执行完成） */
    I2CMSST &= (uint8_t)~0x40;      /* 软件清零 */
}

/* ---------------------------------------------------------------------
 * 上电/热插拔后的 I2C 总线恢复
 *
 * 如果上一次传输被复位打断，从机可能一直拉住 SDA，导致后面所有 START 失败。
 * 这里把 SCL/SDA 当普通 IO，补最多 9 个时钟再手工发一个 STOP，
 * 从机把残余位吐完就会释放总线。
 * 只有 I2C 引脚选在 P3.2/P3.3（CFG_I2C_PIN_SEL=3）时才做。
 * ------------------------------------------------------------------- */
static void i2c_bus_recover(void)
{
#if CFG_I2C_ON_P32_P33
    uint8_t i;

    P3M1 &= (uint8_t)~0x0C;         /* 准双向，可读回电平 */
    P3M0 &= (uint8_t)~0x0C;
    I2C_SDA = 1;
    I2C_SCL = 1;
    delay_us(5);

    if (I2C_SDA == 0)               /* SDA 被拉住才需要打时钟 */
    {
        for (i = 0; i < 9; i++)
        {
            I2C_SCL = 0;
            delay_us(5);
            I2C_SCL = 1;
            delay_us(5);
        }
        I2C_SDA = 0;                /* 手工 STOP：SCL 高时 SDA 上升 */
        delay_us(5);
        I2C_SCL = 1;
        delay_us(5);
        I2C_SDA = 1;
        delay_us(5);
    }
#endif
}

void I2C_Init(void)
{
    i2c_bus_recover();

    P_SW2 |= 0x80;                                                  /* EAXFR = 1 */
    P_SW2 = (uint8_t)((P_SW2 & (uint8_t)~0x30) |
                      (uint8_t)((CFG_I2C_PIN_SEL & 0x03) << 4));    /* 选引脚组 */
    I2CCFG  = (uint8_t)(0xC0 | (EE_I2C_MSSPEED & 0x3F));            /* ENI2C=1 MSSL=1 */
    I2CMSST = 0x00;
}

void I2C_Start(void)            { I2CMSCR = 0x01; i2c_wait(); }
void I2C_Stop(void)             { I2CMSCR = 0x06; i2c_wait(); }
void I2C_SendByte(uint8_t b)    { I2CTXD = b; I2CMSCR = 0x02; i2c_wait(); }
uint8_t I2C_ReceiveByte(void)   { I2CMSCR = 0x04; i2c_wait(); return I2CRXD; }

bit I2C_WaitAck(void)
{
    I2CMSCR = 0x03; i2c_wait();             /* 接收 ACK */
    return (bit)((I2CMSST & 0x02) ? 1 : 0); /* MSACKI=1 -> NACK */
}

void I2C_SendAck(bit nack)
{
    I2CMSST = nack ? 0x01 : 0x00;           /* MSACKO: 1=NACK 0=ACK */
    I2CMSCR = 0x05; i2c_wait();             /* 发送 ACK */
}

/* ---------------------------------------------------------------------
 * 槽位 / 型号
 * ------------------------------------------------------------------- */
static ee_dev_t code *ee_cur(void)
{
    if (!s_valid)
    {
        return 0;
    }
    return &EE_TYPES[s_type];
}

uint8_t EE_Select(uint8_t chip7)
{
    if (chip7 < 0x50 || chip7 > 0x57)
    {
        return 0;
    }

    s_addr7 = chip7;
    s_type  = EE_SLOT_TYPE[chip7 - 0x50];
    if (s_type >= EE_CHIP_COUNT)            /* 配置写错时兜底成 24C64 */
    {
        s_type = EE_CHIP_24C64;
    }
    s_valid = 1;
    return 1;
}

uint32_t EE_MaxAddr(void)
{
    ee_dev_t code *d = ee_cur();

    return (d == 0) ? 0UL : d->max_addr;
}

/* ---------------------------------------------------------------------
 * 调试：只做一次"START + 器件地址 + 等 ACK + STOP"
 * 用来区分"器件不在/地址不对/上拉缺失(NACK)" 和 "通着但数据是 0xFF"
 * ------------------------------------------------------------------- */
static uint8_t ee_sa(ee_dev_t code *d, uint32_t addr, uint8_t rw);

bit EE_Probe(void)
{
    ee_dev_t code *d;

    d = ee_cur();
    if (d == 0)
    {
        return 0;
    }

    I2C_Err = 0;
    I2C_Start();
    I2C_SendByte(ee_sa(d, 0, 0));
    if (I2C_WaitAck())
    {
        I2C_Err = 1;
        I2C_Stop();
        return 0;                       /* NACK：没人应答 */
    }
    I2C_Stop();
    return 1;
}

/* ---------------------------------------------------------------------
 * 调试：扫描 0x50~0x57，返回"哪些地址有器件应答"的位掩码
 *
 * 对多路烧录器很有用：一眼就能看出插了几个工位、地址有没有配错。
 * 扫描过程临时改写 s_addr7 / s_type，结束时恢复，不影响已选槽位。
 * ------------------------------------------------------------------- */
uint8_t EE_ScanMask(void)
{
    uint8_t mask;
    uint8_t keep7;
    uint8_t keeptype;
    uint8_t keepvalid;
    uint8_t i;

    keep7     = s_addr7;
    keeptype  = s_type;
    keepvalid = s_valid;

    mask = 0;
    for (i = 0; i < 8; i++)
    {
        s_addr7 = (uint8_t)(0x50 + i);
        s_type  = EE_SLOT_TYPE[i];
        if (s_type >= EE_CHIP_COUNT)
        {
            s_type = EE_CHIP_24C64;
        }
        s_valid = 1;

        if (EE_Probe())
        {
            mask = (uint8_t)(mask | (1 << i));
        }
    }

    s_addr7   = keep7;
    s_type    = keeptype;
    s_valid   = keepvalid;

    return mask;
}

/* ---------------------------------------------------------------------
 * 器件地址（含 24C04/08/16 的页位、24C1024 的 A16）
 * ------------------------------------------------------------------- */
static uint8_t ee_sa(ee_dev_t code *d, uint32_t addr, uint8_t rw)
{
    uint8_t sa;

    sa = (uint8_t)(s_addr7 << 1);

    if (d->addr_bytes < 2 && d->page_bits != 0)
    {
        uint8_t bits = d->page_bits;
        uint8_t mask = (uint8_t)(((1 << bits) - 1) << 1);
        uint16_t hi  = (uint16_t)(addr >> 8);

        sa = (uint8_t)(sa & (uint8_t)~mask);                             /* 清掉页位 */
        sa = (uint8_t)(sa | (uint8_t)((hi & ((1 << bits) - 1)) << 1));   /* 并入页位 */
    }

    if (d->hi_mask != 0 && (addr & 0x10000UL) != 0)
    {
        sa = (uint8_t)(sa | d->hi_mask);                                 /* A16 */
    }

    return (uint8_t)(sa | rw);
}

/* 发送片内地址字节，全部 ACK 返回 1 */
static bit ee_send_addr(ee_dev_t code *d, uint32_t addr)
{
    if (d->addr_bytes >= 2)
    {
        I2C_SendByte((uint8_t)(addr >> 8));
        if (I2C_WaitAck()) { I2C_Err = 2; return 0; }
    }

    I2C_SendByte((uint8_t)(addr & 0xFF));
    if (I2C_WaitAck()) { I2C_Err = 3; return 0; }

    return 1;
}

/* ---------------------------------------------------------------------
 * 写周期等待
 *
 *   EE_WC_USE_ACK_POLL == 0（默认）：固定延时，时长由 EE_WRITE_CYCLE_MS 决定
 *   EE_WC_USE_ACK_POLL == 1        ：ACK 轮询，写完立刻试地址，
 *                                    收到 ACK 就往下走，整片烧录能快一倍左右
 * ------------------------------------------------------------------- */
#if EE_WC_USE_ACK_POLL
static void ee_wait_cycle(ee_dev_t code *d, uint32_t addr)
{
    uint16_t n;

    n = EE_WC_POLL_MAX;
    while (n--)
    {
        I2C_Start();
        I2C_SendByte(ee_sa(d, addr, 0));
        if (!I2C_WaitAck())         /* 器件回 ACK：内部写周期结束 */
        {
            I2C_Stop();
            return;
        }
        I2C_Stop();
    }
    delay_ms(EE_WRITE_CYCLE_MS);    /* 轮询失败兜底 */
}
#else
/* 宏形式：写成宏函数，改 EE_WRITE_CYCLE_MS 立即生效，且不产生未引用参数告警 */
#define ee_wait_cycle(d, addr)      delay_ms(EE_WRITE_CYCLE_MS)
#endif

/* ---------------------------------------------------------------------
 * 页写（调用者保证 len <= 页内剩余空间）
 * ------------------------------------------------------------------- */
static bit ee_write_page(ee_dev_t code *d, uint32_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    if (len == 0 || len > CFG_PKT_BYTES)    /* 防御：不写空包，也不一次写超过一包 */
    {
        return 0;
    }

    I2C_Err = 0;
    I2C_Start();
    I2C_SendByte(ee_sa(d, addr, 0));
    if (I2C_WaitAck())  { I2C_Err = 1; I2C_Stop(); return 0; }
    if (!ee_send_addr(d, addr)) { I2C_Stop(); return 0; }

    for (i = 0; i < len; i++)
    {
        I2C_SendByte(buf[i]);
        if (I2C_WaitAck()) { I2C_Err = 4; I2C_Stop(); return 0; }
    }

    I2C_Stop();
    return 1;
}

/* ---------------------------------------------------------------------
 * 对外：块写（自动按页拆分）
 * ------------------------------------------------------------------- */
bit EE_WriteBlock(uint32_t addr, const uint8_t *buf, uint8_t len)
{
    ee_dev_t code *d;
    uint16_t space;
    uint8_t chunk;
    uint8_t off;

    d = ee_cur();
    if (d == 0 || len == 0)
    {
        return 0;
    }

    while (len != 0)
    {
        if (addr > d->max_addr)
        {
            return 0;
        }

        off   = (uint8_t)(addr & (uint16_t)(d->page - 1));
        space = (uint16_t)(d->page - off);      /* 注意用 16 位：24C1024 的页内剩余可能是 256 */
        chunk = (space > (uint16_t)len) ? len : (uint8_t)space;

        if (chunk == 0)                         /* 防御：page 配错时立刻失败，绝不原地死循环 */
        {
            return 0;
        }

        if (!ee_write_page(d, addr, buf, chunk))
        {
            return 0;
        }
        ee_wait_cycle(d, addr);

        addr += chunk;
        buf  += chunk;
        len   = (uint8_t)(len - chunk);
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * 对外：顺序读
 * ------------------------------------------------------------------- */
bit EE_ReadBlock(uint32_t addr, uint8_t *buf, uint8_t len)
{
    ee_dev_t code *d;
    uint8_t i;

    d = ee_cur();
    if (d == 0 || len == 0 || addr > d->max_addr)
    {
        return 0;
    }

    I2C_Err = 0;
    I2C_Start();
    I2C_SendByte(ee_sa(d, addr, 0));            /* 假写：送片内地址 */
    if (I2C_WaitAck())          { I2C_Err = 1; I2C_Stop(); return 0; }
    if (!ee_send_addr(d, addr)) { I2C_Stop(); return 0; }

    I2C_Start();                                /* 重复 START */
    I2C_SendByte(ee_sa(d, addr, 1));            /* 读地址 */
    if (I2C_WaitAck())          { I2C_Err = 5; I2C_Stop(); return 0; }

    for (i = 0; i < len; i++)
    {
        buf[i] = I2C_ReceiveByte();
        I2C_SendAck((bit)((i == (uint8_t)(len - 1)) ? 1 : 0));   /* 最后一字节 NACK */
    }

    I2C_Stop();
    return 1;
}
