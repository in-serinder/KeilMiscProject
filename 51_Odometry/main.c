#include "stc89c52.h"
#include "Display.h"
#include "obo.h"
#include "Delay.h"
#include "key.h"
#include "uart.h"

/* ========== EEPROM 地址宏定义（集中管理） ========== */
#define LAST_MODE_ADDR  0x00   // 模式标志 (uint8_t)
// MILES_ADDR(0x01~0x04) 与 EEPROM_TEXT_ADDR(0x10) 见 obo.h

/* ========== 刷新周期宏 ========== */
// 主循环 tick 单位 = 10ms（Timer0驱动sys_tick_10ms）
// REFRESH_INTERVAL_TICK = 刷新间隔多少个tick：
//   50 → 50×10ms = 500ms  (0.5秒刷新，用户要求)
//  100 → 100×10ms = 1000ms (1秒刷新，旧值)
#define REFRESH_INTERVAL_TICK  50

bit Mode_Flag  = 0; // 0:ODO  1:SM
bit Reset_Flag = 0;

uint16_t xdata sys_tick_10ms    = 0;   // 10ms系统tick
uint16_t xdata ModeLastIntTick  = 0;   // 模式键上次中断tick(去抖)

void main(void)
{
    static bit last_Mode_Flag;          // 检测模式变化(避免每次读EEPROM)
    static uint16_t last_500ms_tick;    // 【0.5秒刷新】上一次刷新显示的tick（单位10ms）

    UART_Init();
    I2C_Init();
    LED_Init();
    Key_Init();

    /* ---- 硬件定时器驱动系统滴答 ----
     * 晶振：11.0592MHz  
     * 模式：12T（默认，AUXR保持默认，不要开1T）
     *       ÷12后 = 11059200/12 = 921600 Hz
     * 10ms 需要 tick = 921600 × 0.01 = 9216
     * 重装值 = 65536 - 9216 = 56320 = 0xDC00  
     *   【为什么不开1T？】
     *   1T模式下10ms需要 110592 tick，超过16位最大值65536！装不下。
     *   12T模式0xDC00刚好10ms/次 → sys_tick_10ms 名实相符，
     *   上层所有参数(REFRESH_INTERVAL_TICK=50=0.5s、
     *   KEY_LONGPRESS_CYCLES=300=3s、OBO_DEBOUNCE=2=20ms) 全部不用改。
     */
    AUXR &= 0x7F;         // ★ 关闭1T模式！使用传统12T（与99%8051代码兼容）
    TMOD &= 0xF0;         // Timer0 模式1（16位手动重装）
    TMOD |= 0x01;
    TH0 = 0xDC;           // 高8位
    TL0 = 0x00;           // 低8位  → 0xDC00 = 10ms@11.0592MHz 12T
    TF0 = 0;              // 清溢出标志
    ET0 = 1;              // 使能Timer0中断（之前漏了！定时器中断没开）
    TR0 = 1;              // 启动Timer0

    OBO_Init();
    EA = 1;             // 开启总中断

    DEBUG_SY("Init", "System starting");

    Display_Init();

    {
        uint8_t tmp;
        tmp = EEPROM_ReadByte(LAST_MODE_ADDR);
        Mode_Flag = (tmp & 0x01);
        if (tmp == 0xFF) Mode_Flag = 0;   // 空片默认ODO
    }
    last_Mode_Flag   = Mode_Flag;
    last_500ms_tick  = sys_tick_10ms;     // 同步起始tick（0.5秒刷新）
    DEBUG_RW("EEPROM", "Read stored mode");

    /* 【问题4修复】开机立即点亮对应模式LED */
    if (Mode_Flag)
        LED_SM_Mode();
    else
        LED_Obometry_Mode();

    while (1) {
        //  每10ms执行一次(本循环主体约10ms) 

        OBO_Poll();          // 霍尔脉冲轮询(高电平有效时检测上升沿)

        Reset_Flag = Key_Scan();
        if (Reset_Flag) {
            DEBUG_UO("Mileage", "Reset triggered by long press");

            Display_ResetAnimation();       // 8.8.8.8. 闪烁3次反馈
            OBO_SET_Millis(0);               // 清内存里程+EEPROM里程
            EEPROM_WriteU8 (EEPROM_TEXT_ADDR, 0); // 清开机次数
            DEBUG_RW("EEPROM", "Mileage + boot count erased");

            /* 【问题3修复】显示 SUCCEED 反馈 */
            Display_ShowSuccess();

            /* 立即刷新显示 */
            if (Mode_Flag)
                Display_SM_Mode(OBO_GET_SPEED(), OBO_GET_Miles());
            else
                Display_Obometry_Mode(OBO_GET_Miles());
            Reset_Flag = 0;
            DEBUG_UO("Mileage", "Reset complete");
        }

        if (Mode_Flag != last_Mode_Flag) {
            if (Mode_Flag)
                DEBUG_UO("Mode", "Switch to SM");
            else
                DEBUG_UO("Mode", "Switch to ODO");
            EEPROM_WriteByte(LAST_MODE_ADDR, Mode_Flag);
            DEBUG_RW("EEPROM", "Save mode flag");
            last_Mode_Flag = Mode_Flag;
            // 模式切换立即刷新（不等0.5s）
            {
                float cur_speed = OBO_GET_SPEED();
                if (Mode_Flag)
                    Display_SM_Mode(cur_speed, OBO_GET_Miles());
                else
                    Display_Obometry_Mode(OBO_GET_Miles());
            }
        }

        // 每 REFRESH_INTERVAL_TICK×10ms 刷新一次显示
        if ((uint16_t)(sys_tick_10ms - last_500ms_tick) >= REFRESH_INTERVAL_TICK) {
            last_500ms_tick = sys_tick_10ms;

            /* 先计算速度（同时更新里程滤波），再显示 */
            {
                float cur_speed = OBO_GET_SPEED();
                if (Mode_Flag) {
                    DEBUG_DP("SM", "Update SM mode display");
                    Display_SM_Mode(cur_speed, OBO_GET_Miles());
                } else {
                    DEBUG_DP("ODO", "Update ODO mode display");
                    Display_Obometry_Mode(OBO_GET_Miles());
                }
            }
        }

        Delay_ms(KEY_INTERVAL_MS);
    }
}


/* ======== Timer0 10ms 滴答中断（与上面的12T 0xDC00匹配） ======== */
void Timer0_Isr(void) interrupt 1
{
    TH0 = 0xDC;   // 11.0592MHz 12T 10ms
    TL0 = 0x00;
    sys_tick_10ms++;
}


void INT1_Isr(void) interrupt 2
{
    if ((uint16_t)(sys_tick_10ms - ModeLastIntTick) >= KEY_DEBOUNCE_CYCLES) {
        Mode_Flag = !Mode_Flag;
        ModeLastIntTick = sys_tick_10ms;
    }
}