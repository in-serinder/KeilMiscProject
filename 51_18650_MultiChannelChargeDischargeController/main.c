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

#define BATTER_TH 2.7f
#define REFRESH_MS 1000

uint8_t idata status[4] = {0};
uint16_t idata sys_tick = 0;
uint8_t idata setting_mode = 0;
uint16_t idata both_press_start = 0; // 同时按下的起始tick
bit both_press_active = 0;           // 同时按下已确认

void Timer0_Init(void) {
  AUXR |= 0x80;
  TMOD &= 0xF0;
  TL0 = 0x06;
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
  uint16_t idata last = 0, now;
  char idata title[6];
  char idata right[8];
  uint8_t idata i, cha_th;
  float idata v;
  uint8_t idata vi, vd, p;

  EA = 0;
  SSD13XX_Init();
  SSD13XX_Clear();
  BAT_ADC_Init();
  CHR_Init();
  KEY_Init();
  Timer0_Init();

  for (i = 0; i < 4; i++) {
    CHR_Set_CH(i + 1, 0);
    status[i] = 0;
  }

  EA = 1;

  while (1) {
    now = sys_tick;

    // ★ 关中断保护下进行按键扫描
    KEY_Scan_Handle(now);

    // ★ 非阻塞双键同时按下检测（50ms消抖）
    if ((P32 == 0) && (P33 == 0)) {
      if (!both_press_active) {
        if (both_press_start == 0)
          both_press_start = now;
        else if (now - both_press_start >= 50) {
          // 确认同时按下超过50ms
          both_press_active = 1;
          setting_mode = !setting_mode;
          SSD13XX_Clear();
        }
      }
    } else {
      both_press_start = 0;
      both_press_active = 0;
    }

    // ★ 阈值设置界面
    if (setting_mode) {
      cha_th = KEY_GetThreshold();
      SSD13XX_ClearPage(0);
      SSD13XX_WriteString(0, 0, "Set Thr:", 0);
      SSD13XX_WriteChar(56, 0, '0' + (cha_th / 10), 0);
      SSD13XX_WriteChar(64, 0, '0' + (cha_th % 10), 0);
      SSD13XX_WriteChar(72, 0, '%', 0);
      SSD13XX_WriteString(0, 16, "Both->Back", 0);
      continue;
    }

    if (now - last < REFRESH_MS)
      continue;
    last = now;

    cha_th = KEY_GetThreshold();

    for (i = 0; i < 4; i++) {
      EA = 0;
      v = BAT_ADC_ReadVoltage(i);
      EA = 1;

      title[0] = 'C';
      title[1] = 'h';
      title[2] = ' ';
      title[3] = '1' + i;
      title[4] = '\0';

      if (v < 0.01f) {
        right[0] = '-';
        right[1] = '-';
        right[2] = '-';
        right[3] = '\0';
        // CHR_Set_CH(i + 1, 0);
        CHR_Set_CH(i + 1, 1); // 这里是因为我ADC烧了，但不能浪费通道
        Draw_ProgressBar_Double(0, i * 16, 128, title, "NoSig", right, 0);
        continue;
      }

      vi = (uint8_t)v;
      vd = (uint8_t)((v - vi) * 10);
      p = VoltageToSOC(v);

      right[0] = '0' + vi;
      right[1] = '.';
      right[2] = '0' + vd;
      right[3] = 'V';
      right[4] = '\0';

      if (v < BATTER_TH) {
        CHR_Set_CH(i + 1, 0);
        Draw_ProgressBar_Double(0, i * 16, 128, title, "Low", right, 0);
      } else {
        if (p >= cha_th)
          CHR_Set_CH(i + 1, 0);
        else
          CHR_Set_CH(i + 1, 1);
        Draw_ProgressBar_Double(0, i * 16, 128, title, right, right, p);
      }
    }
  }
}