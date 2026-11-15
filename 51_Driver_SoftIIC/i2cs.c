#include "i2cs.h"

#if I2C_USE_SLAVE

/* ==========================================================================
 * 软 I2C 从机实现（边沿驱动状态机）
 * --------------------------------------------------------------------------
 * 状态迁移：
 *   IDLE --START--> ADDR --收到本机地址--> ACKA(应答) --> RX / TX
 *   RX   每 8 位 -> ACKD(应答) -> RX
 *   TX   每 8 位 -> ACKW(等主机应答) -> TX 或 IDLE
 *   STOP / 重复起始 --> 结束当前帧（先回调，再切状态）
 *
 * 三条不能违反的规则：
 *   1. START/STOP 只在 SCL 为高时成立；SCL 低时 SDA 怎么变都不算。
 *   2. **上升沿采样并计数，下降沿只判断。** 如果把自增写在下降沿，
 *      START 之后那次"进入低电平"的下降沿就会被算成第 1 位，整字节错一位。
 *   3. 自己驱动过 SDA 之后，必须在函数末尾**重新读引脚**再存进 s_last_sda，
 *      否则下一步会把自己的动作误判成 START/STOP，事务被打断。
 * ========================================================================== */

#define ST_IDLE     0
#define ST_ADDR     1
#define ST_ACKA     2   /* 地址应答 */
#define ST_RX       3   /* 收数据 */
#define ST_TX       4   /* 发数据 */
#define ST_ACKD     5   /* 收完一字节后应答 */
#define ST_ACKW     6   /* 发完一字节后等主机应答 */

static unsigned char s_state;
static unsigned char s_bitcnt;
static unsigned char s_shift;
static unsigned char s_last_scl;
static unsigned char s_last_sda;
static unsigned char s_is_read;

static unsigned char s_buf[I2CS_BUF_SIZE];  /* 收/发共用，省 RAM */
static unsigned char s_len;                 /* 已收字节数 / 待发字节数 */
static unsigned char s_tx;                  /* 发送到第几字节 */

#if I2CS_USE_REG_FILE
static unsigned char s_ptr;                 /* 寄存器指针 */
static unsigned char s_regs[I2CS_REG_COUNT];
#endif

static unsigned char s_addr = I2CS_DEFAULT_ADDR;

static I2CS_OnWrite s_on_write;
static I2CS_OnRead  s_on_read;

/* ---------------- 内部 ---------------- */

/* 驱动 SDA。这里**不更新** s_last_sda：边沿检测要和真实引脚电平比，
   统一在 Poll 末尾刷新。 */
static void sda_drive(unsigned char v)
{
    if (v)
        I2CS_SDA = 1;
    else
        I2CS_SDA = 0;
}

static void tx_bit(void)
{
    if (s_shift & 0x80)
        sda_drive(1);
    else
        sda_drive(0);
    s_shift = (unsigned char)(s_shift << 1);
    s_bitcnt++;
}

/* 取下一个要发的字节，并立刻把 MSB 摆到线上，
   赶在主机下一个上升沿之前（主机不会等我们）。 */
static void tx_load(void)
{
    if (s_tx < s_len)
        s_shift = s_buf[s_tx++];
    else
        s_shift = 0xFF;     /* 主机读超了：一直回 FF，不要重复旧数据 */

    s_bitcnt = 0;
    tx_bit();
}

/* 主机要读：先把整个发送缓冲区准备好 */
static void tx_prepare(void)
{
    unsigned char n;
#if I2CS_USE_REG_FILE
    unsigned char i;
#endif

    if (s_on_read)
    {
        n = s_on_read(s_buf, I2CS_BUF_SIZE);
        if (n > I2CS_BUF_SIZE)
            n = I2CS_BUF_SIZE;
        s_len = n;
    }
#if I2CS_USE_REG_FILE
    else
    {
        /* 寄存器模式：从指针开始填满缓冲区，实现自动递增 */
        for (i = 0; i < I2CS_BUF_SIZE; i++)
            s_buf[i] = s_regs[(unsigned char)(s_ptr + i) % I2CS_REG_COUNT];
        s_len = I2CS_BUF_SIZE;
    }
#else
    else
    {
        /* 没装回调也没开寄存器模式：没有什么可给的，回 0xFF 让主机自己判断 */
        s_len = 0;
    }
#endif

    s_tx = 0;
    s_bitcnt = 0;
    s_shift = 0;
    tx_load();
}

/* 一帧写结束（STOP 或重复起始）：交给用户回调，或落进寄存器文件 */
static void frame_end(void)
{
#if I2CS_USE_REG_FILE
    unsigned char i;
#endif

    if (s_len == 0)
        return;

    if (s_on_write)
    {
        s_on_write(s_buf, s_len);
        return;
    }

#if I2CS_USE_REG_FILE
    /* 寄存器模式：第一个字节是寄存器指针，后面才是数据 */
    s_ptr = s_buf[0];
    for (i = 1; i < s_len; i++)
        s_regs[(unsigned char)(s_ptr + i - 1) % I2CS_REG_COUNT] = s_buf[i];
#endif
}

/* ---------------- 对外 ---------------- */

void I2CS_Init(void)
{
#if I2CS_USE_REG_FILE
    unsigned char i;
#endif

    I2CS_SDA = 1;
    I2CS_SCL = 1;

#if I2CS_USE_REG_FILE
    for (i = 0; i < I2CS_REG_COUNT; i++)
        s_regs[i] = 0;
    s_ptr = 0;
#endif

    s_state = ST_IDLE;
    s_bitcnt = 0;
    s_shift = 0;
    s_len = 0;
    s_tx = 0;
    s_is_read = 0;
    s_last_scl = 1;
    s_last_sda = 1;
}

void I2CS_SetAddr(unsigned char addr7)
{
    s_addr = addr7;
}

unsigned char I2CS_Addr(void)
{
    return s_addr;
}

void I2CS_SetHandlers(I2CS_OnWrite on_write, I2CS_OnRead on_read)
{
    s_on_write = on_write;
    s_on_read = on_read;
}

void I2CS_Poll(void)
{
    unsigned char scl = I2CS_SCL ? 1 : 0;
    unsigned char sda = I2CS_SDA ? 1 : 0;

    /* ---- START / STOP：只有 SCL 高的时候才算 ---- */
    if (scl && s_last_scl)
    {
        if (s_last_sda && !sda)             /* SCL 高时 SDA 下降 = START */
        {
            /* 重复起始：上一段写事务到此结束，先把数据交出去 */
            if (s_state == ST_RX || s_state == ST_ACKD)
                frame_end();

            s_state = ST_ADDR;
            s_bitcnt = 0;
            s_shift = 0;
            s_is_read = 0;
            s_len = 0;
            s_tx = 0;
        }
        else if (!s_last_sda && sda)        /* SCL 高时 SDA 上升 = STOP */
        {
            if (s_state == ST_RX || s_state == ST_ACKD)
                frame_end();

            s_state = ST_IDLE;
            s_len = 0;
            s_tx = 0;
            sda_drive(1);
        }
    }

    if (scl && !s_last_scl) /* ---------- 上升沿：采样 + 计数 ---------- */
    {
        switch (s_state)
        {
        case ST_ADDR:
        case ST_RX:
            s_shift = (unsigned char)((s_shift << 1) | (sda ? 1 : 0));
            s_bitcnt++;             /* 计数必须在这一侧 */
            break;

        case ST_ACKW:
            s_shift = sda ? 1 : 0;  /* 主机的应答位：0 = ACK */
            break;

        default:
            break;
        }
    }
    else if (!scl && s_last_scl) /* ---------- 下降沿：只推进 ---------- */
    {
        switch (s_state)
        {
        case ST_ADDR:
            if (s_bitcnt >= 8)      /* 只判断，不自增 */
            {
                unsigned char a;
                a = (unsigned char)(s_shift >> 1);

                if (a == s_addr
#if I2CS_ACCEPT_BCAST
                    || a == 0x00
#endif
                   )
                {
                    s_is_read = (unsigned char)(s_shift & 0x01);
                    s_state = ST_ACKA;
                    sda_drive(0);   /* 拉低 SDA 应答 */
                }
                else
                {
                    s_state = ST_IDLE;  /* 不是我的地址，撒手 */
                }

                s_bitcnt = 0;
                s_shift = 0;
            }
            break;

        case ST_ACKA:
            sda_drive(1);           /* 应答结束，释放总线 */
            s_bitcnt = 0;
            s_shift = 0;

            if (s_is_read)
            {
                tx_prepare();
                s_state = ST_TX;
            }
            else
            {
                s_len = 0;          /* 开始收新帧 */
                s_tx = 0;
                s_state = ST_RX;
            }
            break;

        case ST_RX:
            if (s_bitcnt >= 8)
            {
                if (s_len < I2CS_BUF_SIZE)
                    s_buf[s_len++] = s_shift;

                s_bitcnt = 0;
                s_shift = 0;
                s_state = ST_ACKD;
                sda_drive(0);
            }
            break;

        case ST_ACKD:
            sda_drive(1);
            s_bitcnt = 0;
            s_shift = 0;
            s_state = ST_RX;
            break;

        case ST_TX:
            if (s_bitcnt >= 8)
            {
                sda_drive(1);       /* 8 位发完，放开给主机应答 */
                s_state = ST_ACKW;
            }
            else
            {
                tx_bit();
            }
            break;

        case ST_ACKW:
            if (s_shift == 0)       /* 主机 ACK：继续发下一字节 */
            {
                s_state = ST_TX;
                tx_load();
            }
            else                    /* 主机 NACK：主机不要了 */
            {
                sda_drive(1);
                s_state = ST_IDLE;
            }
            break;

        default:
            break;
        }
    }

    s_last_scl = scl;
    /* 重新读引脚：既包含主机刚刚的改变，也包含本函数自己驱动的结果 */
    s_last_sda = I2CS_SDA ? 1 : 0;
}

bit I2CS_BusIdle(void)
{
    return (bit)(s_state == ST_IDLE);
}

unsigned char I2CS_State(void)
{
    return s_state;
}

#if I2CS_USE_REG_FILE
unsigned char I2CS_GetReg(unsigned char i)
{
    return s_regs[i % I2CS_REG_COUNT];
}

void I2CS_SetReg(unsigned char i, unsigned char v)
{
    s_regs[i % I2CS_REG_COUNT] = v;
}
#endif /* I2CS_USE_REG_FILE */

#endif /* I2C_USE_SLAVE */
