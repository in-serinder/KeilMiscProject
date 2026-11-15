#include "i2c_demo.h"
#include "i2cm.h"
#include "i2cs.h"

/* ==========================================================================
 * 示例：一片做主、一片做从，用软 I2C 互传数据
 * --------------------------------------------------------------------------
 * 这个文件里没有一行时序代码 —— 它在用库，不是驱动本身。
 * 日志输出是可选的（I2C_DEMO_LOG），关掉之后整个工程与串口无关。
 * ========================================================================== */

#if I2C_DEMO_LOG
#include "uart.h"
#define LOG_S(s)    UART_SendString(s)
#define LOG_H(b)    UART_SendHex(b)
#define LOG_C(c)    UART_SendByte(c)
#else
#define LOG_S(s)    do { } while (0)
#define LOG_H(b)    do { } while (0)
#define LOG_C(c)    do { } while (0)
#endif

static void put_u8(unsigned char v)
{
    LOG_C((unsigned char)('0' + v / 100));
    LOG_C((unsigned char)('0' + (v / 10) % 10));
    LOG_C((unsigned char)('0' + v % 10));
}

#if I2C_USE_MASTER && I2C_DEMO_AS_MASTER
static void put_u16(unsigned int v)
{
    LOG_C((unsigned char)('0' + v / 10000));
    LOG_C((unsigned char)('0' + (v / 1000) % 10));
    LOG_C((unsigned char)('0' + (v / 100) % 10));
    LOG_C((unsigned char)('0' + (v / 10) % 10));
    LOG_C((unsigned char)('0' + v % 10));
}
#endif

static void put_hex_n(unsigned char *p, unsigned char n)
{
    unsigned char i;
    for (i = 0; i < n; i++)
    {
        LOG_H(p[i]);
        LOG_C(' ');
    }
}

#if I2C_USE_MASTER && I2C_DEMO_AS_MASTER
/* 空闲延时。顺便喂一下本机的从机引擎 —— 这样一块板子既能主动发命令，
   又能在别人叫它的时候应答，两个方向都能通信（总线空闲时不会有冲突）。 */
static void demo_delay(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 110; j++)
        {
#if I2C_USE_SLAVE
            I2CS_Poll();
#endif
        }
}
#endif

/* ==========================================================================
 * 从机侧
 * ========================================================================== */
#if I2C_USE_SLAVE

static unsigned char s_shared[I2C_DEMO_DATA_MAX];  /* 从机对外暴露的"共享内存" */
static unsigned char s_shared_len;
static unsigned char s_frames;                      /* 收到多少帧 */
static unsigned char s_last_cmd;

/* 主机写完一帧后由库回调。注意：这是**中断上下文式**的短回调，
   里面绝对不能有串口打印这种慢操作，否则下一位就丢了，只做内存拷贝。 */
static void demo_on_write(unsigned char *buf, unsigned char len)
{
    unsigned char i, n;

    s_frames++;
    s_last_cmd = buf[0];

    if (buf[0] != I2C_CMD_WRITE && buf[0] != I2C_CMD_PING)
        return;
    if (len < 2)
        return;

    n = (unsigned char)(len - 2);
    if (n > I2C_DEMO_DATA_MAX)
        n = I2C_DEMO_DATA_MAX;
    s_shared_len = n;
    for (i = 0; i < n; i++)
        s_shared[i] = buf[2 + i];
}

/* 主机要读时由库回调，把要发的字节填进 buf，返回字节数 */
static unsigned char demo_on_read(unsigned char *buf, unsigned char max)
{
    unsigned char i, n;

    if (max < 2)
        return 0;

    buf[0] = I2C_CMD_READ;
    buf[1] = s_shared_len;

    n = (unsigned char)(2 + s_shared_len);
    if (n > max)
        n = max;

    for (i = 0; i < s_shared_len && (unsigned char)(2 + i) < max; i++)
        buf[2 + i] = s_shared[i];

    return n;
}

void I2C_DemoSlaveInit(void)
{
    I2CS_SetAddr(I2C_DEMO_ADDR);
    I2CS_SetHandlers(demo_on_write, demo_on_read);
}

#if I2C_USE_SLAVE && (!I2C_USE_MASTER || !I2C_DEMO_AS_MASTER)
void I2C_DemoSlave(void)
{
    unsigned char last = 0xFF;

    LOG_S("\r\nslave up, addr 0x");
    LOG_H(I2CS_Addr());
    LOG_S(", polling...\r\n");

    while (1)
    {
        I2CS_Poll();

        /* 慢操作（串口 / 计算）只能放在总线空闲的时候做。
           打印一行大约 20~30ms，主机侧必须在事务之间留出间隔。 */
        if (I2CS_BusIdle() && s_frames != last)
        {
            last = s_frames;
            LOG_S("    frame #");
            put_u8(s_frames);
            LOG_S(" cmd=");
            LOG_H(s_last_cmd);
            LOG_S(" data=");
            put_hex_n(s_shared, s_shared_len);
            LOG_S(" state=");
            LOG_H(I2CS_State());
            LOG_S("\r\n");
        }
    }
}
#endif

#endif /* I2C_USE_SLAVE */

/* ==========================================================================
 * 主机侧
 * ========================================================================== */
#if I2C_USE_MASTER && I2C_DEMO_AS_MASTER

void I2C_DemoMaster(void)
{
    unsigned char i, cnt, ok, found[4], cmd;
    unsigned char wbuf[I2C_DEMO_DATA_MAX + 2];
    unsigned char rbuf[I2C_DEMO_DATA_MAX + 2];
    unsigned int round = 0;
    unsigned int good = 0, bad = 0;

    /* 速率按需要设：对端是软件从机就必须慢，对端是 24Cxx 之类可以快 */
    I2CM_SetSpeed(I2C_DEMO_SPEED);

    /* ---- [1] 扫描：多机环境下先确认谁在线 ---- */
    LOG_S("\r\n[1] bus scan\r\n");
    cnt = I2CM_Scan(found, 4);
    if (cnt == 0)
    {
        LOG_S("    nobody acked, check SDA/SCL/GND and slave power\r\n");
    }
    else
    {
        for (i = 0; i < cnt; i++)
        {
            LOG_S("    found 0x");
            LOG_H(found[i]);
            LOG_S("\r\n");
        }
    }

    /* ---- [2] 写一帧，再读回来比对 ---- */
    LOG_S("[2] write 4 bytes then read them back\r\n");

    wbuf[0] = I2C_CMD_WRITE;
    wbuf[1] = 4;
    wbuf[2] = 0xA1;
    wbuf[3] = 0xB2;
    wbuf[4] = 0xC3;
    wbuf[5] = 0xD4;

    if (!I2CM_WriteFrame(I2C_DEMO_ADDR, wbuf, 6))
    {
        LOG_S("    write NACK, err=");
        LOG_H(I2CM_LastError());
#if I2C_SELFTEST
        LOG_S(" slave-state=");
        LOG_H(I2CS_State());
#endif
        LOG_S("\r\n");
    }
    else
    {
        LOG_S("    write ACK\r\n");
        demo_delay(2);

        if (!I2CM_ReadFrame(I2C_DEMO_ADDR, rbuf, 6))
        {
            LOG_S("    read NACK, err=");
            LOG_H(I2CM_LastError());
            LOG_S("\r\n");
        }
        else
        {
            LOG_S("    read  ");
            put_hex_n(rbuf, 6);
            ok = (bit)(rbuf[0] == I2C_CMD_READ && rbuf[1] == 4 &&
                       rbuf[2] == 0xA1 && rbuf[3] == 0xB2 &&
                       rbuf[4] == 0xC3 && rbuf[5] == 0xD4);
            if (ok)
                LOG_S("-> MATCH\r\n");
            else
                LOG_S("-> MISMATCH\r\n");
        }
    }

    /* ---- [3] 一条事务里"先写后读"：发读命令，重复起始，再收数据 ----
       这是库最通用的一条接口，任何"命令 + 应答"的协议都用它。 */
    LOG_S("[3] WriteRead: send CMD_READ, repeated start, then read\r\n");

    cmd = I2C_CMD_READ;
    if (I2CM_WriteRead(I2C_DEMO_ADDR, &cmd, 1, rbuf, 6))
    {
        LOG_S("    read  ");
        put_hex_n(rbuf, 6);
        if (rbuf[0] == I2C_CMD_READ)
            LOG_S("-> MATCH\r\n");
        else
            LOG_S("-> MISMATCH\r\n");
    }
    else
    {
        LOG_S("    NACK, err=");
        LOG_H(I2CM_LastError());
        LOG_S("\r\n");
    }

#if I2C_USE_SLAVE
    /* 本机自己的从机引擎状态：单板自测时它就是真的从机，
       双机主机板上它应该一直是 idle=1 state=0，否则说明状态机被卡住了。 */
    LOG_S("    own slave: idle=");
    if (I2CS_BusIdle())
        LOG_C('1');
    else
        LOG_C('0');
    LOG_S(" state=");
    LOG_H(I2CS_State());
    LOG_S("\r\n");
#endif

    /* ---- [4] 循环：每轮换一组数据，写完立刻读回来核对 ---- */
    LOG_S("[4] loop, one round per ~200ms\r\n");

    while (1)
    {
        wbuf[0] = I2C_CMD_WRITE;
        wbuf[1] = 4;
        wbuf[2] = (unsigned char)round;
        wbuf[3] = (unsigned char)(round >> 8);
        wbuf[4] = (unsigned char)(round ^ 0x5A);
        wbuf[5] = (unsigned char)(0xFF - (round & 0xFF));

        ok = 0;
        if (I2CM_WriteFrame(I2C_DEMO_ADDR, wbuf, 6))
        {
            demo_delay(2);
            if (I2CM_ReadFrame(I2C_DEMO_ADDR, rbuf, 6))
            {
                ok = 1;
                for (i = 2; i < 6; i++)
                    if (rbuf[i] != wbuf[i])
                        ok = 0;
            }
        }

        if (ok)
        {
            good++;
        }
        else
        {
            bad++;
            LOG_S("    round ");
            put_u16(round);
            LOG_S(" FAIL wrote ");
            put_hex_n(&wbuf[2], 4);
            LOG_S("read ");
            put_hex_n(&rbuf[2], 4);
            LOG_S("err=");
            LOG_H(I2CM_LastError());
            LOG_S("\r\n");
        }

        /* 每 64 轮报一次成绩，别把串口刷满 */
        if ((round & 0x003F) == 0x003F)
        {
            LOG_S("    ok=");
            put_u16(good);
            LOG_S("  bad=");
            put_u16(bad);
            LOG_S("  speed=");
            put_u8(I2CM_Speed());
            LOG_S("\r\n");
        }

        round++;
        demo_delay(200);
    }
}

#endif /* I2C_USE_MASTER */
