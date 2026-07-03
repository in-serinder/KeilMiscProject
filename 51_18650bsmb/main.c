#include "BAT.h"
#include "CHR.h"
#include "delay.h"
#include "key.h"
#include "ssd13xx.h"

#define BATTER_TH 2.7f           // 电池低压保护阈值
#define BATTERY_CHS 4            // 充电通道总数
#define REFRESH_INTERVAL 1000    // 屏幕/采集刷新间隔ms
#define THRESHOLD_SHOW_HOLD 1000 // 松开后阈值文字停留时长

// 全局仅保留状态数组（需要跨循环保存）
uint8_t idata percent_status[BATTERY_CHS] = {0}; // 1充电中 0已充满
uint16_t idata sys_tick = 0;

void Timer0_ISR(void) interrupt 1 {
  TH0 = 0xFF;
  TL0 = 0x06; // STC15 1ms重载值
  sys_tick++;
}

void main(void) {
  uint8_t idata i;
  float idata voltage, percent;
  char idata ch_buffer[4], percent_buffer[5], voltage_buffer[5],
      threshold_buffer[8];
  uint8_t idata threshold, new_threshold;
  uint16_t idata last_refresh_tick = 0;
  uint16_t idata key_release_tick =
      0; // 记录按键松开时刻，用于判断是否需要显示阈值文字
  uint8_t idata show_threshold_flag = 0; // 是否需要显示阈值文字

  // 外设初始化
  SSD13XX_Init();
  SSD13XX_Clear();
  SSD13XX_WriteString(0, 20, "Initializing..", 0);
  BAT_ADC_Init();
  CHR_Init();
  KEY_Init();
  Timer0_Init(); // 初始化1ms定时器

  while (1) {
    KEY_Scan_Handle();
    new_threshold = KEY_GetThreshold();

    // 按键调节
    if (threshold != new_threshold) {
      threshold = new_threshold;
      show_threshold_flag = 1;
      key_release_tick = sys_tick; // 刷新松开计时起点
      // 局部刷新阈值页面
      SSD13XX_ClearPage(1);
      sprintf(threshold_buffer, "Th: %d", threshold);
      SSD13XX_WriteString(0, 20, threshold_buffer, 0);
    } else if (show_threshold_flag) {
      if (sys_tick - key_release_tick >= THRESHOLD_SHOW_HOLD) {
        show_threshold_flag = 0;
        SSD13XX_ClearPage(1); // 停留时间到，清空阈值区域
      }
    }

    // 定时采集刷新充电通道（全程无阻塞）
    if (sys_tick - last_refresh_tick < REFRESH_INTERVAL) {
      continue;
    }
    last_refresh_tick = sys_tick;

    for (i = 0; i < BATTERY_CHS; i++) {
      voltage = BAT_ADC_ReadVoltage(i);
      percent = VoltageToSOC(voltage);

      // 低压保护：电压低于2.7V，停止充电
      if (voltage < BATTER_TH) {
        CHR_Set_CH(i + 1, 0);
        sprintf(ch_buffer, "Ch %d", i);
        sprintf(percent_buffer, "Low Volt");
        sprintf(voltage_buffer, "%.2fV", voltage);
        Draw_ProgressBar_Double(0, i * 16, 128, ch_buffer, percent_buffer,
                                voltage_buffer, 0);
        percent_status[i] = 0;
        continue;
      }

      // 电池百分比高于阈值：充满，停止充电
      if (percent > threshold) {
        if (percent_status[i] == 0)
          continue;

        CHR_Set_CH(i + 1, 0);
        sprintf(ch_buffer, "Ch %d", i);
        sprintf(percent_buffer, "%.1f%%(Over)", percent);
        sprintf(voltage_buffer, "%.2fV", voltage);
        Draw_ProgressBar_Double(0, i * 16, 128, ch_buffer, percent_buffer,
                                voltage_buffer, (uint8_t)(percent * 100.0f));
        percent_status[i] = 0;
        continue;
      }

      // 低于阈值，开启充电
      if (percent_status[i] == 0)
        percent_status[i] = 1;
      CHR_Set_CH(i + 1, 1);

      sprintf(ch_buffer, "Ch %d", i);
      sprintf(percent_buffer, "%.1f%%", percent);
      sprintf(voltage_buffer, "%.2fV", voltage);
      Draw_ProgressBar_Double(0, i * 16, 128, ch_buffer, percent_buffer,
                              voltage_buffer, (uint8_t)(percent * 100.0f));
    }
  }
}