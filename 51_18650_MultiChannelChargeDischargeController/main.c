// #include "Delay.h"
// #include "ssd13xx.h"
// void main(void) {
//   SSD13XX_Init();
//   SSD13XX_Clear();
//   Draw_ProgressBar_Double(0, 0, 128, "CH1", "3.7V", "BAT:", 30);
//   Draw_ProgressBar_Double(0, 16, 128, "CH2", "4.1V", "BAT:", 60);
//   Draw_ProgressBar_Double(0, 32, 128, "CH3", "3.3V", "BAT:", 85);
//   Draw_ProgressBar_Double(0, 48, 128, "CH4", "3.9V", "BAT:", 50);
//   while (1);
// }


#include "BAT.h"
#include "CHR.h"
#include "delay.h"
#include "key.h"
#include "ssd13xx.h"

#define BATTER_TH          2.7f     // 电池检测阈值(V)
#define STOP_SOC           65       // 停止充电电量(%)
#define RESUME_SOC         58       // 恢复充电电量(%)
#define REFRESH_MS         1000     // 主循环检测周期(ms)
#define MEASURE_DELAY_MS   20       // 离线测量等待时间(ms)
#define TH_OVERFLOW        3        // 阈值溢出补偿（可忽略）

// 按键长按时间定义（用于设置模式）
#define BOTH_PRESS_MS      50

uint8_t idata status[4] = {0};          // 通道状态
uint16_t idata sys_tick = 0;            // 系统节拍计数器
uint8_t idata setting_mode = 0;         // 0=正常模式, 1=阈值设置模式
uint8_t idata cha_th = 65;              // 充电目标阈值(%)

// 同时按下检测
uint16_t idata both_press_start = 0;
bit both_press_active = 0;

// 每个通道的CE状态（1=充电，0=停充）
static bit ce_state[4] = {1, 1, 1, 1};

void Timer0_Init(void) {
    AUXR |= 0x80;       // 定时器0 1T模式
    TMOD &= 0xF0;
    TL0 = 0x06;         // 1ms @ 11.0592MHz
    TH0 = 0xFF;
    TF0 = 0;
    TR0 = 1;
    ET0 = 1;
}

void Timer0_ISR(void) interrupt 1 {
    TH0 = 0xFF;
    TL0 = 0x06;
    sys_tick++;
}

void main(void) {
    uint16_t last_update = 0;
    uint8_t i;
    float v;
    uint8_t soc;
    char title[6];
    char right[8];


    EA = 0;
    SSD13XX_Init();
    SSD13XX_Clear();
    BAT_ADC_Init();
    CHR_Init();
    KEY_Init();
    Timer0_Init();

    // 关闭所有通道充电
    for (i = 0; i < 4; i++) {
        CHR_Set_CH(i + 1, 0);
        status[i] = 0;
        ce_state[i] = 0;
    }

    EA = 1;


    while (1) {
        //按键扫描（含设置模式切换）
        KEY_Scan_Handle(sys_tick);

        // 检测双键同时按下（P32和P33）
        if ((P32 == 0) && (P33 == 0)) {
            if (!both_press_active) {
                if (both_press_start == 0)
                    both_press_start = sys_tick;
                else if (sys_tick - both_press_start >= BOTH_PRESS_MS) {
                    both_press_active = 1;
                    setting_mode = !setting_mode;
                    SSD13XX_Clear();
                }
            }
        } else {
            both_press_start = 0;
            both_press_active = 0;
        }

        // 设置模式
        if (setting_mode) {
            // 显示当前阈值
            SSD13XX_ClearPage(0);
            SSD13XX_WriteString(0, 0, "Set Thr:", 0);
            SSD13XX_WriteChar(56, 0, '0' + (cha_th / 10), 0);
            SSD13XX_WriteChar(64, 0, '0' + (cha_th % 10), 0);
            SSD13XX_WriteChar(72, 0, '%', 0);
            SSD13XX_WriteString(0, 16, "Both->Back", 0);
            // 注意：阈值的调整由 KEY_GetThreshold() 在按键中断中完成
            // 这里只需显示当前值
            continue;   // 不执行充电控制
        }

        if ((uint16_t)(sys_tick - last_update) < REFRESH_MS)
            continue;
        last_update = sys_tick;

        cha_th = KEY_GetThreshold();   // 假设该函数返回0-100

        for (i = 0; i < 4; i++) {
            CHR_Set_CH(i + 1, 0);

            delay_ms(MEASURE_DELAY_MS);

            EA = 0;
            v = BAT_ADC_ReadVoltage(i);
            EA = 1;

            // 组装显示标题
            title[0] = 'C';
            title[1] = 'h';
            title[2] = ' ';
            title[3] = '1' + i;
            title[4] = '\0';

            // 无电池或接触不良
            if (v < 0.01f) {
                ce_state[i] = 0;   // 关闭充电
                right[0] = '-';
                right[1] = '-';
                right[2] = '-';
                right[3] = '\0';
                Draw_ProgressBar_Double(0, i * 16, 128, title, "NoSig", right, 0);
                continue;
            }

            //低于电池检测阈值
            if (v < BATTER_TH) {
                ce_state[i] = 0;   // 关闭充电（或保持关闭）
                // 显示电压
                uint8_t vi = (uint8_t)v;
                uint8_t vd = (uint8_t)((v - vi) * 10);
                right[0] = '0' + vi;
                right[1] = '.';
                right[2] = '0' + vd;
                right[3] = 'V';
                right[4] = '\0';
                Draw_ProgressBar_Double(0, i * 16, 128, title, "Low", right, 0);
                continue;
            }

            soc = VoltageToSOC(v);

            // 滞回决策 ----------
            if (soc >= cha_th) {            
                ce_state[i] = 0;            
            } else if (soc <= (cha_th - 7)) { // 恢复充电阈值 = 目标 - 7%
                ce_state[i] = 1;            // 恢复充电
            }
            // 否则保持原有状态

            CHR_Set_CH(i + 1, ce_state[i]);

            uint8_t vi = (uint8_t)v;
            uint8_t vd = (uint8_t)((v - vi) * 10);
            right[0] = '0' + vi;
            right[1] = '.';
            right[2] = '0' + vd;
            right[3] = 'V';
            right[4] = '\0';
            Draw_ProgressBar_Double(0, i * 16, 128, title, right, right, soc);
        }
    }
}


    //   right[0] = '0' + vi;
    //   right[1] = '.';
    //   right[2] = '0' + vd;
    //   right[3] = 'V';
    //   right[4] = '\0';

    //   if (v < BATTER_TH) {
    //     CHR_Set_CH(i + 1, 0);
    //     Draw_ProgressBar_Double(0, i * 16, 128, title, "Low", right, 0);
    //   } else {
    //     if (p >= cha_th)
    //       CHR_Set_CH(i + 1, 0);
    //     else
    //       CHR_Set_CH(i + 1, 1);
    //     Draw_ProgressBar_Double(0, i * 16, 128, title, right, right, p);
    //   }
    // }

    // for (i = 0; i < 4; i++) {
    //   EA = 0;
    //   v = BAT_ADC_ReadVoltage(i);
    //   EA = 1;

    //   title[0] = 'C';
    //   title[1] = 'h';
    //   title[2] = ' ';
    //   title[3] = '1' + i;
    //   title[4] = '\0';

    //   if (v < 0.01f) {
    //     right[0] = '-';
    //     right[1] = '-';
    //     right[2] = '-';
    //     right[3] = '\0';
    //     // CHR_Set_CH(i + 1, 0);
    //     CHR_Set_CH(i + 1, 1); // 这里是因为我ADC烧了，但不能浪费通道
    //     Draw_ProgressBar_Double(0, i * 16, 128, title, "NoSig", right, 0);
    //     continue;
    //   }

    //   vi = (uint8_t)v;
    //   vd = (uint8_t)((v - vi) * 10);
    //   p = VoltageToSOC(v);

    //   right[0] = '0' + vi;
    //   right[1] = '.';
    //   right[2] = '0' + vd;
    //   right[3] = 'V';
    //   right[4] = '\0';

    //   if (v < BATTER_TH) {
    //     CHR_Set_CH(i + 1, 0);
    //     Draw_ProgressBar_Double(0, i * 16, 128, title, "Low", right, 0);
    //   } else {
    //     if (p >= cha_th)
    //       CHR_Set_CH(i + 1, 0);
    //     else
    //       CHR_Set_CH(i + 1, 1);
    //     Draw_ProgressBar_Double(0, i * 16, 128, title, right, right, p);
    //   }
    // }
  