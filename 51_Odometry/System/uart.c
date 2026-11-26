#include "uart.h"
#include "obo.h"

/* 1 = 启用串口命令（需要串口接收中断）
 * 0 = 完全不接收，只保留上电的 RDY 握手（出问题时的保险版本） */
#define UART_CMD 1

/* ===========================================================================
 *  串口 9600 8N1，Timer1 方式2，全 12T
 *
 *  发送：查询法，带最大等待次数，绝不死等。
 *  接收：中断里只做一件事——把字节搬进缓冲区，绝不碰 TI。
 *
 *  【重要】中断里用到的每一个变量都必须是 data / bit：
 *    COMPACT 模型下变量默认在 pdata，而显式 xdata 变量一次访问就是
 *    MOV DPTR,#addr + MOVX，执行 MOVX 期间 P2 会输出 DPH(=0x00)，
 *    而 MAX7219 的 LOAD/CLK/DIN 正是 P2.2/P2.3/P2.4，
 *    帧被这一下打断就是"数码管偶发整屏不亮"的根源。
 *    data/bit 用直接寻址和位指令，完全不动 P2。
 *
 *  命令（ASCII，大小写都行）：
 *    M<米数>   把总里程设为该值，支持 0~2 位小数   M1234  M1234.5  m888
 *    R         回读当前里程            → MIL 1234.00m
 *    C         清零（里程 + 开机次数）  → OK 0.00m
 *    V         固件版本                → V3
 *    H  或  ?  命令帮助
 *  设置类命令回复： OK 1234.00m（写后已回读校验）/ E2（EEPROM 写坏）/ ERR（语法错）
 *  其它字符开头的行一律回 ERR，避免误打当成没反应。
 *
 *  很多串口助手默认不勾"发送新行"，所以不强制要求 \r\n：
 *  一行字符到齐后 300ms 内没有新字节，就自动提交（见 UART_Poll）。
 * ========================================================================= */

#define CMD_MAX 16

/* 回显开关：1 = 把收到的字节原样发回（串口助手关掉"本地回显"后还能看到字符，
 *            就证明 PC→MCU 这条线是通的）；0 = 不回显，只收命令。 */
#define UART_ECHO 1

/* 串口写完里程后立刻刷新数码管，不用等主循环 0.5s 那一拍。
 * 只声明用到的接口，避免为一个刷新动作引入 Display.h 的整条依赖链。 */
extern bit  Mode_Flag;
extern void Display_SM_Mode(float speed, float mil);
extern void Display_Obometry_Mode(float mil);
extern uint16_t data sys_tick_10ms;      /* main.c 里的 10ms 节拍 */

#if UART_CMD
static uint8_t data cmd_buf[CMD_MAX];
static uint8_t data cmd_len   = 0;
static bit          cmd_ready = 0;
static bit          cmd_oflow = 0;   /* 本行超长：回车后回 ERR，不当成新命令 */
static uint16_t data last_rx_tick = 0;   /* 最后一次收到字节的时刻 */

#if UART_ECHO
static uint8_t data echo_buf[16];
static uint8_t data echo_n = 0;
#endif
#endif


/* ---- 发送：查询 + 上限 ---- */
static void UART_Put(uint8_t b)
{
    uint16_t g;
    bit es_save;

    /* 发送期间不让串口中断插进来：TI 只由这里清，
     * 中断里若也去清 TI，发送方就永远等不到它。 */
    es_save = ES;
    ES = 0;

    TI  = 0;
    SBUF = b;
    for (g = 0; g < 1500; g++) {
        if (TI) break;
    }
    TI = 0;

    ES = es_save;
}

static void UART_PutStr(char *s)
{
    while (*s) UART_Put((uint8_t)*s++);
}

#if UART_CMD
static void UART_PutU32(uint32_t v)
{
    uint8_t data buf[10];
    uint8_t i = 0;

    if (v == 0) { UART_Put('0'); return; }
    while (v > 0 && i < 10) {
        buf[i++] = (uint8_t)('0' + (uint8_t)(v % 10UL));
        v /= 10UL;
    }
    while (i > 0) UART_Put(buf[--i]);
}

/* 打印 "1234.00m" */
static void UART_PutMeters(uint32_t cm)
{
    UART_PutU32(cm / 100UL);
    UART_Put('.');
    UART_Put((uint8_t)('0' + (uint8_t)((cm % 100UL) / 10UL)));
    UART_Put((uint8_t)('0' + (uint8_t)(cm % 10UL)));
    UART_PutStr("m");
}

/* 串口改完里程后马上把新值显示出来 */
static void UART_Refresh(void)
{
    if (Mode_Flag)
        Display_SM_Mode(OBO_GET_SPEED(), OBO_GET_Miles());
    else
        Display_Obometry_Mode(OBO_GET_Miles());
}
#endif  /* UART_CMD */

void UART_Init(void)            /* 11.0592MHz, 9600bps, 12T */
{
    SCON  = 0x50;       /* 方式1，REN=1 */
    PCON &= 0x7F;       /* SMOD=0 */
    AUXR &= 0xBF;       /* T1x12=0，Timer1 走 12T（TH1=0xFD 才是 9600） */
    AUXR &= 0xFE;       /* S1ST2=0，串口时钟用 Timer1 而不是 Timer2 */

    TMOD &= 0x0F;
    TMOD |= 0x20;       /* Timer1 方式2，8位自动重装 */
    TL1 = 0xFD;
    TH1 = 0xFD;
    ET1 = 0;
    TR1 = 1;

    TI  = 0;
    RI  = 0;
#if UART_CMD
    cmd_len   = 0;
    cmd_ready = 0;
    ES = 1;             /* 接收走中断，发送仍是查询 */
#else
    ES = 0;
#endif

    UART_PutStr("RDY\r\n");   /* 上电握手：终端能看到 RDY 就说明波特率和链路都对 */
}


#if UART_CMD

/* ---- 接收中断：只搬字节，不碰 TI ---- */
void UART_Isr(void) interrupt 4
{
    uint8_t c;

    if (RI) {
        RI = 0;
        c = SBUF;
        last_rx_tick = sys_tick_10ms;

#if UART_ECHO
        if (echo_n < sizeof(echo_buf)) echo_buf[echo_n++] = c;
#endif

        if (c == '\r' || c == '\n') {
            if (cmd_len || cmd_oflow) cmd_ready = 1;
        } else if (cmd_len < (CMD_MAX - 1)) {
            cmd_buf[cmd_len++] = c;
        } else {
            cmd_oflow = 1;            /* 超长行整行丢掉 */
        }
    }

    /* 发送期间 UART_Put 已把 ES 关掉，中断不可能在这里抢到 TI。
     * 万一 TI 残留（发送等待超时退出），必须在这里清掉：
     * TI 一直为 1 会让串口中断反复触发，主循环被饿死，表现就是"发什么都没反应"。 */
    if (TI) TI = 0;
}


/* ---- 命令解析 ---- */
static void UART_Handle(void)
{
    uint8_t len, i, ch, dec, dec_n, dot;
    uint32_t m, cm;

    len = cmd_len;
    cmd_len = 0;

    if (cmd_oflow) {                        /* 上一行超长，整行作废 */
        cmd_oflow = 0;
        UART_PutStr("ERR\r\n");
        return;
    }
    if (len < 1) return;

    ch = cmd_buf[0];

    if (ch == 'V' || ch == 'v') { UART_PutStr("V3\r\n"); return; }

    if (ch == '?' || ch == 'H' || ch == 'h') {
        UART_PutStr("M<meter> set  R read  C clear\r\n");
        UART_PutStr("V version     ? help\r\n");
        return;
    }

    if (ch == 'R' || ch == 'r') {
        UART_PutStr("MIL ");
        UART_PutMeters(OBO_GET_Millis());
        UART_PutStr("\r\n");
        return;
    }

    if (ch == 'C' || ch == 'c') {           /* 清零：与长按复位一致，里程 + 开机次数 */
        OBO_SET_Millis(0);
        EEPROM_WriteByte(EEPROM_TEXT_ADDR, 0);
        UART_PutStr("OK 0.00m\r\n");
        UART_Refresh();
        return;
    }

    if (ch != 'M' && ch != 'm') { UART_PutStr("ERR\r\n"); return; }

    if (len < 2) { UART_PutStr("ERR\r\n"); return; }

    m = 0; dec = 0; dec_n = 0; dot = 0;
    for (i = 1; i < len; i++) {
        ch = cmd_buf[i];

        /* 兼容手输时照抄帮助格式：M<1234>、M 1234、m:1234 里的括号/空格/冒号直接跳过 */
        if (ch == '<' || ch == '>' || ch == ' ' || ch == ':' || ch == '=') {
            continue;
        }

        if (ch == '.') {
            if (dot) { UART_PutStr("ERR\r\n"); return; }
            dot = 1;
        } else if (ch < '0' || ch > '9') {
            UART_PutStr("ERR\r\n"); return;
        } else if (!dot) {
            m = m * 10UL + (uint32_t)(ch - '0');
            if (m > 999999UL) { UART_PutStr("ERR\r\n"); return; }
        } else if (dec_n < 2) {
            dec = (uint8_t)(dec * 10 + (uint8_t)(ch - '0'));
            dec_n++;
        }
    }

    if (dec_n == 1) dec *= 10;                   /* 一位小数 = 十分之一米 */
    cm = m * 100UL + (uint32_t)dec;              /* 内部单位 cm */

    OBO_SET_Millis(cm);                          /* 同时写回 EEPROM */

    /* 写后回读校验：EEPROM 写坏了要能看出来，而不是以为写成功 */
    if (EEPROM_ReadU32(MILES_ADDR) != cm) {
        UART_PutStr("E2\r\n");
        UART_Refresh();
        return;
    }

    UART_PutStr("OK ");
    UART_PutMeters(cm);
    UART_PutStr("\r\n");
    UART_Refresh();
}


void UART_Poll(void)
{
#if UART_ECHO
    uint8_t i, n;
#endif

    /* 很多串口助手默认不勾"发送新行"，所以不能只靠 \r\n 断行：
     * 300ms 内没有新字节，就把已收到的当成一整行提交。 */
    if (!cmd_ready && (cmd_len || cmd_oflow)) {
        if ((uint16_t)(sys_tick_10ms - last_rx_tick) >= 30) cmd_ready = 1;
    }

    if (!cmd_ready) return;

    ES = 0;                 /* 解析期间别再进接收中断，避免半行被改 */
    cmd_ready = 0;

#if UART_ECHO
    /* 等这一行收完再回显，不和接收抢时序。
     * 串口助手把"本地回显"关掉后这里还能看到自己敲的字符 → RX 线是通的。 */
    n = echo_n;
    echo_n = 0;
    for (i = 0; i < n; i++) UART_Put(echo_buf[i]);
#endif

    UART_Handle();
    cmd_len   = 0;
    cmd_oflow = 0;
    ES = 1;
}

#else   /* !UART_CMD */

void UART_Poll(void)
{
}

#endif  /* UART_CMD */


/* ---- 调试输出（UART_DEBUG=0 时整段不参与编译） ---- */
#if UART_DEBUG

void UART_SendByte(uint8_t byte)      { UART_Put(byte); }
void UART_SendString(uint8_t *str)    { UART_PutStr((char *)str); }
void UART_SendU16(uint16_t val)       { UART_PutU32((uint32_t)val); }
void UART_SendU32(uint32_t val)       { UART_PutU32(val); }

void UART_SendFloat2(float val)
{
    uint16_t int_part;
    uint8_t frac;

    if (val < 0) { UART_Put('-'); val = -val; }
    int_part = (uint16_t)val;
    UART_PutU32((uint32_t)int_part);
    UART_Put('.');
    frac = (uint8_t)((val - (float)int_part) * 100.0f + 0.5f);
    UART_Put((uint8_t)('0' + (frac / 10)));
    UART_Put((uint8_t)('0' + (frac % 10)));
}

#endif
