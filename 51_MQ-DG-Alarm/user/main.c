/* =====================================================================
 * main.c  —  MQ 传感器阈值报警器
 *
 * 功能：
 *   采集模式：四位数码管持续显示 MQ 的 ADC 采样值（0~4095），
 *             采样值 >= 阈值时有源蜂鸣器（P3.3 高电平）长鸣，
 *             回落到 阈值-CFG_TH_HYST 以下才停，避免阈值附近抖。
 *   设置模式：按一次 add/sub 进入，数码管闪烁显示阈值（250ms 亮/灭），
 *             期间可继续用 add/sub 加减（按住 0.5s 后每 100ms 连发），
 *             3 秒无操作自动退回采集模式；阈值改过则在退出时写入 EEPROM。
 *
 * 定时基准：
 *   Timer0 工作在 1T + 16 位自动重装，1ms 中断一次，主循环用时间戳
 *   做"协作式调度"（按键 10ms / 采样 200ms / 刷新 50ms），不用阻塞延时。
 *
 * 显示刷新：
 *   段码先与缓存比对，内容没变就不发总线帧（TM1637 一帧约 80us，
 *   闪烁时每秒要改 4 次，做差分能明显减少总线占用和干扰）。
 * ===================================================================== */
#include "board.h"
#include "delay.h"
#include "TM1637.h"
#include "adc.h"
#include "key.h"
#include "eeprom.h"
#if CFG_DEBUG_UART
#include "uart.h"
#endif

#define MODE_MEAS               0
#define MODE_SET                1

/* Timer0 1ms：22118400 / 1000 = 22118 个计数，65536-22118 = 43418 = 0xA99A */
#define T0_RELOAD_H             0xA9
#define T0_RELOAD_L             0x9A

static volatile uint16_t s_ms;              /* 上电以来的毫秒数 */

static uint8_t  s_seg[4];                   /* 显示缓存（差分刷新用） */
static uint8_t  s_seg_ok;

static uint16_t s_th;                       /* 当前阈值 */
static uint16_t s_th_bak;                   /* 进入设置模式时的阈值，用于判断"改过没有" */
static uint16_t s_meas;                     /* 采集值（一阶滤波后） */
static uint8_t  s_mode;                     /* MODE_MEAS / MODE_SET */
static uint16_t s_set_left;                 /* 设置模式剩余时间 ms */
static uint8_t  s_buzz;                     /* 蜂鸣器当前状态 */
static uint8_t  s_adc_err;                  /* 1 = ADC 转换超时（不是输入 0V） */
static uint8_t  s_no_adc;                   /* 1 = 片上没有 ADC 模块 */

#if CFG_KEY_RAW_SHOW
static uint16_t s_raw_hold;                 /* 原始键值显示的保持时间 ms */
#endif

/* ------------------------------------------------------------------ */
/* 定时器                                                              */
/* ------------------------------------------------------------------ */
static void Timer0_Init(void)
{
    AUXR |= 0x80;               /* T0 用 1T 模式 */
    TMOD &= 0xF0;               /* T0 -> 模式 0（STC8 里是 16 位自动重装） */
    TL0 = T0_RELOAD_L;
    TH0 = T0_RELOAD_H;
    ET0 = 1;
    TR0 = 1;
}

void Timer0_Isr(void) interrupt 1
{
    s_ms++;
}

/* 16 位计数器跨中断读取会撕裂，读的时候短暂关掉 T0 中断 */
static uint16_t Now_ms(void)
{
    uint16_t t;

    ET0 = 0;
    t = s_ms;
    ET0 = 1;
    return t;
}

/* ------------------------------------------------------------------ */
/* 显示                                                                */
/* ------------------------------------------------------------------ */
static void Disp_Show(const uint8_t *seg)
{
    uint8_t i;
    uint8_t same = 1;

    if (s_seg_ok)
    {
        for (i = 0; i < 4; i++)
        {
            if (s_seg[i] != seg[i])
            {
                same = 0;
                break;
            }
        }
    }
    else
    {
        same = 0;
    }

    if (same)
    {
        return;
    }

    for (i = 0; i < 4; i++)
    {
        s_seg[i] = seg[i];
    }
    s_seg_ok = 1;
    TM1637_WriteSegs(s_seg, 4);
}

/* 十进制，前导零熄灭（0 显示为 "   0"） */
static void Disp_Num(uint16_t v)
{
    uint8_t seg[4];
    uint8_t i;
    uint16_t t = v;

    for (i = 0; i < 4; i++)
    {
        seg[3 - i] = TM1637_CharToSeg((char)('0' + (uint8_t)(t % 10)));
        t = (uint16_t)(t / 10);
    }

    for (i = 0; i < 3; i++)
    {
        if (seg[i] == 0x3F)         /* '0' 的段码 */
        {
            seg[i] = 0x00;
        }
        else
        {
            break;
        }
    }

    Disp_Show(seg);
}

static void Disp_Blank(void)
{
    uint8_t seg[4];
    uint8_t i;

    for (i = 0; i < 4; i++)
    {
        seg[i] = 0x00;
    }
    Disp_Show(seg);
}

/* ADC 转换超时（不是输入 0V）：显示 "Err"，一眼和数据为 0 区分开 */
static void Disp_Err(void)
{
    uint8_t seg[4];

    seg[0] = 0x00;
    seg[1] = TM1637_CharToSeg('E');
    seg[2] = TM1637_CharToSeg('r');
    seg[3] = TM1637_CharToSeg('r');
    Disp_Show(seg);
}

/* 片上根本没有 ADC 模块（如 STC8G1K08-8Pin 无 A）：显示 "noAd" */
static void Disp_NoAd(void)
{
    uint8_t seg[4];

    seg[0] = TM1637_CharToSeg('n');
    seg[1] = TM1637_CharToSeg('o');
    seg[2] = TM1637_CharToSeg('A');
    seg[3] = TM1637_CharToSeg('d');
    Disp_Show(seg);
}

#if CFG_KEY_RAW_SHOW
/* 键位自检：显示 TM1637 读回的 16 位原始键值 */
static void Disp_Hex(uint16_t v)
{
    uint8_t seg[4];
    uint8_t i, n;

    for (i = 0; i < 4; i++)
    {
        n = (uint8_t)(v & 0x0F);
        v = (uint16_t)(v >> 4);
        seg[3 - i] = TM1637_CharToSeg((n < 10) ? (char)('0' + n) : (char)('A' + n - 10));
    }
    Disp_Show(seg);
}
#endif

/* ------------------------------------------------------------------ */
/* 业务                                                                */
/* ------------------------------------------------------------------ */

/* 阈值加减一次，并重置"3 秒无操作"计时 */
static void Th_Adjust(uint8_t up)
{
    if (s_mode != MODE_SET)
    {
        s_th_bak = s_th;            /* 记住进入设置模式前的值 */
        s_mode = MODE_SET;
    }

    if (up)
    {
        if ((uint16_t)(s_th + CFG_TH_STEP) <= CFG_TH_MAX)
        {
            s_th = (uint16_t)(s_th + CFG_TH_STEP);
        }
        else
        {
            s_th = CFG_TH_MAX;
        }
    }
    else
    {
        if (s_th >= CFG_TH_STEP)
        {
            s_th = (uint16_t)(s_th - CFG_TH_STEP);
        }
        else
        {
            s_th = 0;
        }
    }

    s_set_left = CFG_SET_TIMEOUT_MS;
}

/* 按一次触发一次；按住 500ms 后每 100ms 再触发一次。
 * 返回 1 表示"这一拍该执行一次" */
static uint8_t Key_Repeat(uint8_t down, uint16_t *hold, uint8_t *fired)
{
    if (down == 0)
    {
        *hold = 0;
        *fired = 0;
        return 0;
    }

    if (*hold < 60000)
    {
        *hold = (uint16_t)(*hold + 10);
    }

    if (*fired == 0)
    {
        *fired = 1;                 /* 首次按下立刻响应 */
        return 1;
    }

    if ((*hold >= 500) && (((*hold - 500) % 100) == 0))
    {
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
void main(void)
{
    uint16_t t_now;
    uint16_t t_key = 0;
    uint16_t t_adc = 0;
    uint16_t t_disp = 0;
#if CFG_DEBUG_UART
    uint16_t t_dbg = 0;
#endif
    uint8_t  keys;
    uint8_t  keys_prev = 0;
    uint16_t hold_a = 0;
    uint16_t hold_s = 0;
    uint8_t  fire_a = 0;
    uint8_t  fire_s = 0;
    uint16_t v;
    uint8_t  first = 1;
    uint16_t r2;
    uint16_t r10;
    uint16_t r15;

    Board_Init();

#if CFG_DEBUG_UART
    /* 串口尽早初始化：后面每一步自检都能打出来，屏幕全黑也不至于抓瞎 */
    Uart1_Init();
    Uart1_SendCRLF();
    Uart1_SendStr("== MQ-DG-Alarm boot ==");
    Uart1_SendCRLF();
    Uart1_SendStr("P3M1=");
    Uart1_SendHex8(P3M1);
    Uart1_SendStr(" P3M0=");
    Uart1_SendHex8(P3M0);
    Uart1_SendStr(" P5M1=");
    Uart1_SendHex8(P5M1);
    Uart1_SendStr(" P5M0=");
    Uart1_SendHex8(P5M0);
    Uart1_SendStr(" P_SW2=");
    Uart1_SendHex8(P_SW2);
    Uart1_SendCRLF();
#endif

    /* 上电自检：全段点亮 500ms（某位/某段不亮就是焊点或段码线问题）。
     * colon=1 让中间那位的小数点也一起亮，等于 8 段全测一遍。 */
    TM1637_Init();
    TM1637_Show4("8888", 1);
    s_seg_ok = 0;                   /* 绕过差分缓存，让主循环第一次刷新重发 */
    delay_ms(500);
    TM1637_Clear();

    /* 阈值：EEPROM 有效就用存的，否则用默认值 */
    if (!EE_LoadThreshold(&s_th))
    {
        s_th = CFG_TH_DEFAULT;
#if CFG_DEBUG_UART
        Uart1_SendStr("th=eeprom-invalid -> default ");
        Uart1_SendDec(CFG_TH_DEFAULT);
        Uart1_SendCRLF();
#endif
    }
#if CFG_DEBUG_UART
    else
    {
        Uart1_SendStr("th=eeprom ");
        Uart1_SendDec(s_th);
        Uart1_SendCRLF();
    }
#endif
    s_th_bak = s_th;
    s_mode = MODE_MEAS;
    s_buzz = 0;

    ADC_Init();

    /* 片上到底有没有 ADC：STC8G1K08-8Pin（无 A）没有，ADC_CONTR 读回 0。
     * 有 ADC 再继续探测通道：ch2 = 1K08A 的 P3.2；ch10 = 15 通道片子的 P3.2；
     * ch15 = 内部 1.19V 基准，与外部引脚无关，用来判断"ADC 到底有没有在转换"。 */
#if CFG_MQ_DIGITAL
    s_no_adc = 0;                   /* 强制数字 DO 模式，本来就不用 ADC */
#elif CFG_MQ_AUTO_DO
    s_no_adc = (uint8_t)(((ADC_IsPresent() == 0) && (ADC_IsDigital() == 0)) ? 1 : 0);
#else
    s_no_adc = (uint8_t)((ADC_IsPresent() == 0) ? 1 : 0);
#endif

    if (s_no_adc != 0)
    {
        r2 = 0xFFFF;
        r10 = 0xFFFF;
        r15 = 0xFFFF;
        ADC_SetCh(MQ_ADC_CH);
    }
    else
    {
        r2  = ADC_RawRead(2);
        r10 = ADC_RawRead(10);
        r15 = ADC_RawRead(15);

        if (r2 != 0xFFFF)
        {
            ADC_SetCh(2);
        }
        else if (r10 != 0xFFFF)
        {
            ADC_SetCh(10);
        }
        else
        {
            ADC_SetCh(MQ_ADC_CH);
        }
    }

#if CFG_DEBUG_UART
#if CFG_MQ_DIGITAL
    Uart1_SendStr("mq=digital DO on P3.2");
    Uart1_SendCRLF();
#else
    Uart1_SendStr("adc=");
    Uart1_SendStr((ADC_IsPresent() != 0) ? "ok" : "NOT-WORKING");
    Uart1_SendStr(" cfg=");
    Uart1_SendHex8(ADC_GetCfg());
#if CFG_MQ_AUTO_DO || CFG_MQ_DIGITAL
    Uart1_SendStr(" mq=");
    Uart1_SendStr((ADC_IsDigital() != 0) ? "digital-DO" : "analog-ADC");
#endif
    Uart1_SendCRLF();
    /* 寄存器自检：con(写0A) / con(写82后) / ADCCFG / ADCTIM / P3M1 对照 */
    Uart1_SendStr("reg con0A=");
    Uart1_SendHex8(ADC_ProbeReg(0));
    Uart1_SendStr(" con82=");
    Uart1_SendHex8(ADC_ProbeReg(1));
    Uart1_SendStr(" cfg=");
    Uart1_SendHex8(ADC_ProbeReg(2));
    Uart1_SendStr(" tim=");
    Uart1_SendHex8(ADC_ProbeReg(3));
    Uart1_SendStr(" p3m1=");
    Uart1_SendHex8(ADC_ProbeReg(4));
    Uart1_SendStr(" ie2=");
    Uart1_SendHex8(ADC_ProbeReg(5));
    Uart1_SendStr(" wdt=");
    Uart1_SendHex8(ADC_ProbeReg(6));
    Uart1_SendStr(" clk=");
    Uart1_SendHex8(ADC_ProbeReg(7));
    Uart1_SendStr(" conNoE=");
    Uart1_SendHex8(ADC_ProbeReg(8));
    Uart1_SendCRLF();
    Uart1_SendStr("probe ch2=");
    Uart1_SendHex8((uint8_t)(r2 >> 8));
    Uart1_SendHex8((uint8_t)r2);
    Uart1_SendStr(" ch10=");
    Uart1_SendHex8((uint8_t)(r10 >> 8));
    Uart1_SendHex8((uint8_t)r10);
    Uart1_SendStr(" ref15=");
    Uart1_SendHex8((uint8_t)(r15 >> 8));
    Uart1_SendHex8((uint8_t)r15);
    Uart1_SendStr(" -> use ch");
    Uart1_SendDec(ADC_GetCh());
    Uart1_SendCRLF();
    Uart1_SendStr("ADCCFG=");
    Uart1_SendHex8(ADCCFG);
    Uart1_SendStr(" ADCTIM=");
    Uart1_SendHex8(ADCTIM);
    Uart1_SendStr(" ADC_CONTR=");
    Uart1_SendHex8(ADC_CONTR);
    Uart1_SendCRLF();
#endif
#endif

    Timer0_Init();
    EA = 1;                         /* 采样与显示初始化完再开总中断 */

    while (1)
    {
        t_now = Now_ms();

        /* ---------------- 10ms：按键 / 倒计时 / 报警 ---------------- */
        if ((uint16_t)(t_now - t_key) >= 10)
        {
            t_key = t_now;
            keys = Key_Update();

#if CFG_DEBUG_UART
            if (keys != keys_prev)
            {
                Uart1_SendStr("key raw=");
                Uart1_SendHex8((uint8_t)(Key_LastRaw() >> 8));
                Uart1_SendHex8((uint8_t)Key_LastRaw());
                Uart1_SendStr(" st=");
                Uart1_SendDec(keys);
                Uart1_SendCRLF();
                keys_prev = keys;
            }
#endif

            if (Key_Repeat((uint8_t)(keys & KEY_ADD), &hold_a, &fire_a))
            {
                Th_Adjust(1);
            }
            if (Key_Repeat((uint8_t)(keys & KEY_SUB), &hold_s, &fire_s))
            {
                Th_Adjust(0);
            }

            /* 设置模式：3 秒无操作退回采集模式，改过就存 EEPROM */
            if (s_mode == MODE_SET)
            {
                if (s_set_left > 10)
                {
                    s_set_left = (uint16_t)(s_set_left - 10);
                }
                else
                {
                    s_set_left = 0;
                    s_mode = MODE_MEAS;
                    if (s_th != s_th_bak)
                    {
                        uint8_t ok;

                        ok = EE_SaveThreshold(s_th);
                        if (ok)
                        {
                            s_th_bak = s_th;
                        }
#if CFG_DEBUG_UART
                        Uart1_SendStr("save th=");
                        Uart1_SendDec(s_th);
                        Uart1_SendStr(ok ? " ok" : " FAIL");
                        Uart1_SendStr(" cmdfail=");
                        Uart1_SendDec(EE_LastFail());
                        Uart1_SendCRLF();
#endif
                    }
                }
            }

            /* 报警输出：采集模式下才响，设置模式静音（避免调阈值时被吵）；
             * ADC 没转换成功时也不响 —— 那时候的 s_meas 是不可信的旧值 */
            if ((s_mode == MODE_MEAS) && (s_adc_err == 0))
            {
                if ((s_buzz == 0) && (s_meas >= s_th))
                {
                    s_buzz = 1;
                }
                else if ((s_buzz != 0) && ((uint16_t)(s_meas + CFG_TH_HYST) < s_th))
                {
                    s_buzz = 0;
                }
            }
            else
            {
                s_buzz = 0;
            }
            BEEP = s_buzz;

#if CFG_KEY_RAW_SHOW
            if (keys != 0)
            {
                s_raw_hold = 2000;  /* 按键期间 + 松开后 2s 显示原始键值，够看清 */
            }
            else if (s_raw_hold >= 10)
            {
                s_raw_hold = (uint16_t)(s_raw_hold - 10);
            }
#endif
        }

        /* ---------------- 采样 MQ：正常 200ms；出错后放宽到 1s 重试 ----------------
         * ADC 真的没在转换时，每次重试要空转 8×4ms 阻塞主循环，不能 200ms 来一次。 */
        if ((uint16_t)(t_now - t_adc) >= ((s_adc_err != 0) ? 1000 : 200))
        {
            t_adc = t_now;
            v = MQ_Read();

            if (v == 0xFFFF)        /* 转换没完成，保持上一次的显示数据不动 */
            {
                s_adc_err = 1;
            }
            else
            {
                s_adc_err = 0;
                if (first)
                {
                    s_meas = v;     /* 第一拍直接取，避免从 0 慢慢爬上来 */
                    first = 0;
                }
                else
                {
                    /* 一阶滤波：新值占 1/4，抑制传感器噪声导致的末位乱跳 */
                    s_meas = (uint16_t)((((unsigned long)s_meas * 3UL) + (unsigned long)v) >> 2);
                }
            }
        }

        /* ---------------- 1s：串口状态行 ---------------- */
#if CFG_DEBUG_UART
        if ((uint16_t)(t_now - t_dbg) >= 1000)
        {
            t_dbg = t_now;

            Uart1_SendStr("v=");
            Uart1_SendDec(s_meas);
            Uart1_SendStr(" err=");
            Uart1_SendDec(s_adc_err);
            Uart1_SendStr(" ch=");
            Uart1_SendDec(ADC_GetCh());
            Uart1_SendStr(" to=");
            Uart1_SendDec(ADC_LastTo());
            Uart1_SendStr(" g=");
            Uart1_SendDec(ADC_LastGuard());
            Uart1_SendStr(" ctr=");
            Uart1_SendHex8(ADC_CONTR);
            Uart1_SendStr(" key=");
            Uart1_SendHex8((uint8_t)(Key_LastRaw() >> 8));
            Uart1_SendHex8((uint8_t)Key_LastRaw());
            Uart1_SendStr(" th=");
            Uart1_SendDec(s_th);
            Uart1_SendStr(" m=");
            Uart1_SendDec(s_mode);
#if CFG_MQ_AUTO_DO || CFG_MQ_DIGITAL
            Uart1_SendStr(" d=");
            Uart1_SendDec(ADC_IsDigital());
#endif
            Uart1_SendStr(" b=");
            Uart1_SendDec(s_buzz);
            Uart1_SendCRLF();
        }
#endif

        /* ---------------- 50ms：刷新显示 ---------------- */
        if ((uint16_t)(t_now - t_disp) >= 50)
        {
            t_disp = t_now;

#if CFG_KEY_RAW_SHOW
            if (s_raw_hold != 0)
            {
                Disp_Hex(Key_LastRaw());
            }
            else
#endif
            if (s_no_adc != 0)
            {
                Disp_NoAd();        /* 片上没有 ADC：换 STC8G1K08A，或用数字 DO 模式 */
            }
            else if (s_adc_err)
            {
                Disp_Err();
            }
            else if (s_mode == MODE_MEAS)
            {
                Disp_Num(s_meas);
            }
            else if (((s_set_left / CFG_BLINK_MS) & 0x01) == 0)
            {
                Disp_Num(s_th);     /* 闪烁的"亮"半周 */
            }
            else
            {
                Disp_Blank();       /* 闪烁的"灭"半周 */
            }
        }
    }
}
