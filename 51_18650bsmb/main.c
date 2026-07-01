#include "BAT.h"
#include "CHR.h"
#include "delay.h"
#include "key.h"
#include "ssd1306.h"

#define BATTER_TH 2.7f // 检测是否为电池
#define BATTERY_CHS 4

uint8_t i;
float voltage, percent;
char xdata ch_buffer[4], percent_buffer[5], voltage_buffer[5];

void main(void) {

  SSD13XX_Init();

  // 初始化屏幕显示
  SSD13XX_Clear();
  SSD13XX_WriteString(0, 20, "Initializing..");
  BAT_ADC_Init();
  CHR_Init();
  KEY_Init();

  while (1) {
    // 对通道扫描
    for (i = 0; i < BATTERY_CHS; i++) {
      voltage = BAT_ADC_ReadVoltage(i);
      percent = VoltageToSOC(voltage);
      sprintf(ch_buffer, "Ch %d", i);
      sprintf(percent_buffer, "%.1f%%", percent);
      sprintf(voltage_buffer, "%.2fV", voltage);
      Draw_ProgressBar_Double(0, i * 16, 128, ch_buffer, percent_buffer,
                              voltage_buffer, (uint8_t)(percent * 100.0f));

      delay_ms(1000); // 延时1秒
    }
  }
}
