#include "obo.h"
#include "uart.h"

/* ======== 内部变量 ======== */
uint32_t idata raw_mm       = 0;    // 总脉冲累计 (cm, 用于速度计算)
uint32_t idata valid_mm     = 0;    // 有效骑行里程 (cm, 筛掉推行/静止)
uint32_t idata last_raw_mm  = 0;    // 上次速度计算时的 raw_mm

uint16_t idata write_count  = 0;
uint16_t idata slow_count   = 0;
float    idata speed_kmh    = 0.0f;
uint16_t xdata last_pulse_tick = 0;


#define WIN_TICK  100    // 1秒窗口
#define CYC_LEN   10     // 10个1s为一个周期

static uint32_t wnd_raw  = 0;     // 窗口快照 raw_mm
static uint16_t wnd_tick = 0;     // 窗口起始 tick
static uint16_t wnd_pulse = 0;    // 窗口内脉冲计数（调试用）

static float    xdata cyc_buf[CYC_LEN]; // 环形缓冲（10个1s历史）
static float    cyc_sum   = 0.0f;          // 缓冲的总和
static uint8_t  cyc_idx   = 0;             // 下一个要覆盖的槽位
static uint8_t  cyc_valid = 0;             // 已填充槽位数(1~10)

#if OBO_ACTIVE_HIGH
bit obo_prev_state = 0;            
#endif

extern uint16_t xdata sys_tick_10ms;


/*  脉冲记数 */
static void OBO_CountPulse(void)
{
    last_pulse_tick = sys_tick_10ms;
    if (raw_mm < 0xFFFFFFFFUL - OBO_RATE)
        raw_mm += OBO_RATE;
    wnd_pulse++;   /* 统计本窗口脉冲数 */
}


void OBO_Init(void)
{
    uint8_t boot_cnt, i;

#if OBO_ACTIVE_HIGH
    /* ---- 高电平有效（传感器输出高表示有脉冲）----
     * STC89C52 INT0仅支持下降沿触发，所以不能用中断，
     * 改用主循环轮询检测上升沿。
     * 硬件需要：P3.2 外部加 10kΩ 下拉电阻到GND！
     * 常态=低电平(0)，脉冲到来=高电平(1)                */
    OBO_PIN = 1;            // 准双向口输入态(内部上拉关闭，靠外部下拉)
    obo_prev_state = OBO_PIN;
    DEBUG_SY("OBO", "Polling mode (ACTIVE HIGH, ext pull-down 10k req)");
#else
    IT0 = 1;                // INT0(P3.2)下降沿触发
    EX0 = 1;                // 使能INT0中断
    OBO_PIN = 1;            // 准双向口输入态(内部上拉)
    DEBUG_SY("OBO", "Interrupt mode (falling edge)");
#endif

    /*  开机次数 */
    boot_cnt = EEPROM_ReadU8(EEPROM_TEXT_ADDR);
    UART_SendString("[Debug] [RW] BootCount: read=");
    UART_SendU16(boot_cnt);
    if (boot_cnt == 0xFF) boot_cnt = 0;
    boot_cnt++;
    EEPROM_WriteU8(EEPROM_TEXT_ADDR, boot_cnt);
    UART_SendString(" -> written=");
    UART_SendU16(boot_cnt);
    UART_SendString("\r\n");

    /*  恢复里程 */
    Delay_ms(10);
    raw_mm = EEPROM_ReadU32(MILES_ADDR);
    valid_mm    = raw_mm;               // 启动时两者一致
    last_raw_mm = raw_mm;
    last_pulse_tick = 0;
    wnd_raw  = raw_mm;
    wnd_tick = 0;
    wnd_pulse = 0;
    cyc_sum   = 0.0f;
    cyc_idx   = 0;
    cyc_valid = 0;
    speed_kmh = 0.0f;
    for (i = 0; i < CYC_LEN; i++) cyc_buf[i] = 0.0f;
    DEBUG_RW("OBO", "Restored mileage from EEPROM");
}


/*  主动轮询 */
void OBO_Poll(void)
{
#if OBO_ACTIVE_HIGH
    bit cur = OBO_PIN;
    /* 检测上升沿: 当前=1 且 上次=0 */
    if (cur && !obo_prev_state) {
        if ((uint16_t)(sys_tick_10ms - last_pulse_tick) >= OBO_DEBOUNCE_TICK) {
            OBO_CountPulse();
        }
    }
    obo_prev_state = cur;
#else
    /* 中断模式无需轮询，此函数为空 */
    ;
#endif
}


/*  置零 */
void OBO_SET_Millis(uint32_t ms)
{
    uint8_t i;
    DEBUG_RW("OBO", "Reset mileage to 0");
    raw_mm      = ms;
    valid_mm    = ms;
    last_raw_mm = ms;
    speed_kmh   = 0.0f;
    write_count = 0;
    slow_count  = 0;
    wnd_raw  = ms;
    wnd_tick = 0;
    wnd_pulse = 0;
    cyc_sum   = 0.0f;
    cyc_idx   = 0;
    cyc_valid = 0;
    for (i = 0; i < CYC_LEN; i++) cyc_buf[i] = 0.0f;
    EEPROM_WriteU32(MILES_ADDR, ms);
    DEBUG_RW("OBO", "Mileage saved to EEPROM");
}


/*  读取有效里程 (km) */
float OBO_GET_Miles(void)
{
    return (float)valid_mm / 100000.0f;
}


/*  计算速度 + 里程滤波 + 周期性存EEPROM */
float OBO_GET_SPEED(void)
{
    float spd;
    uint16_t now, elp;
    uint8_t riding;

    now = sys_tick_10ms;

    /*  1s窗口到期：算一次原始速度 */
    elp = (uint16_t)(now - wnd_tick);
    if (elp >= WIN_TICK) {
        uint32_t delta_mm;
        if (raw_mm >= wnd_raw)
            delta_mm = raw_mm - wnd_raw;
        else
            delta_mm = 0;
        spd = (float)delta_mm * 3.6f / (float)elp;
        wnd_raw  = raw_mm;
        wnd_tick = now;


        UART_SendString("[W] elp="); UART_SendU16(elp);
        UART_SendString(" pls="); UART_SendU16(wnd_pulse);
        UART_SendString(" dlt="); UART_SendU32(delta_mm);
        UART_SendString(" spd="); UART_SendFloat2(spd);

        /*  尖峰剔除：1秒内脉冲数超过上限 → 整帧强制归零 */
        if (wnd_pulse > MAX_PULSES_PER_WINDOW) {
            spd = 0.0f;
            UART_SendString(" SPIKE!");
        }
        wnd_pulse = 0;

        /*  10秒环形缓冲（真正的滑动平均）
         *   cyc_buf[0~9]：最近10个1s窗口的速度
         *   工作时：旧值被挤出，新值加入，保持总和
         *   - 有脉冲时：spd > 0，正常入队
         *   - 无脉冲时：spd = 0，同样入队（自然衰减）
         *   - 尖峰被剔除后也=0
         *   - 效果：停脚后每过1秒丢掉一个旧值、加入一个0
         *           速度每秒钟下降1/10，10秒平滑归零
         */
        cyc_sum -= cyc_buf[cyc_idx];   // 挤出最旧的值
        cyc_buf[cyc_idx] = spd;         // 填入最新的值
        cyc_sum += spd;                 // 累加新值
        cyc_idx++;
        if (cyc_idx >= CYC_LEN) cyc_idx = 0;
        if (cyc_valid < CYC_LEN) cyc_valid++;

        speed_kmh = cyc_sum / (float)cyc_valid;

        /* 调试：缓冲状态 */
        UART_SendString(" vld="); UART_SendU16(cyc_valid);
        UART_SendString(" sum="); UART_SendFloat2(cyc_sum);
        UART_SendString(" out="); UART_SendFloat2(speed_kmh);
        UART_SendString("\r\n");
    }

    /*  零速限幅 */
    if (speed_kmh < SPEED_ZERO_KMH) speed_kmh = 0.0f;

    /*  推行/静止滤波 & 有效里程 */
    if (speed_kmh >= WALKING_SPEED_KMH) {
        riding = 1; slow_count = 0;
    } else {
        riding = 0; slow_count++;
    }
    if (riding)
        valid_mm += (uint32_t)(raw_mm - last_raw_mm);

    last_raw_mm = raw_mm;

    /* EEPROM 周期性保存 */
    write_count++;
    if (write_count >= EEPROM_SAVE_CYCLES) {
        write_count = 0;
        if (speed_kmh > 0.5f || (COUNT_SLOW && slow_count >= COUNT_SLOW_PATIENCE)) {
            EEPROM_WriteU32(MILES_ADDR, valid_mm);
            slow_count = 0;
        }
    }
    return speed_kmh;
}


#if !OBO_ACTIVE_HIGH
void INT0_Isr(void) interrupt 0
{
    if ((uint16_t)(sys_tick_10ms - last_pulse_tick) >= OBO_DEBOUNCE_TICK) {
        OBO_CountPulse();
    }
}
#endif