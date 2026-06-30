#include "Display.h"
#include "LCD1602.h"
#include <stdint.h>

static char idata buffer[16];

void Display_Init(void) {
  // Initialization code for the display
  LCD_Init();
}

void Display_BootMessage(void) {
  LCD_ShowString(1, 1, "FakeLoad Boot");
  LCD_ShowString(2, 1, "Initializing...");
}

// 显示假负载功率列表
// 20w 40w 60w
// void Display_FakeLoadList(unsigned char *loadList) {
//   uint8_t i, longbit = 0, offset = 0;
//   uint16_t idata temp;

//   for (i = 0; i < 3; i++) {
//     temp = loadList[i] * 100;
//     while (temp) {
//       temp /= 10;
//       longbit++;
//     }
//     // 5.24 还原为 524 /10 -> 52 /10 ->5 /10 ->0
//     // 25.45 还原为 2545

//     LCD_ShowString(1, offset, loadList[i] + '0');
//     LCD_ShowChar(1, offset + longbit + 1, 'W');
//     // 上次位置 加符号加空格
//     offset += longbit + 2;
//   }
// }

void Display_FakeLoad(float power) {
  buffer[0] = '\0';
  sprintf(buffer, "Power: %.2fW", power);
  LCD_ShowString(1, 1, buffer);
}

// Timer: 分钟:秒
void Display_TimerSetupMessage(uint16_t set_seconds) {
  buffer[0] = '\0';
  uint8_t minutes = set_seconds / 60;
  uint8_t seconds = set_seconds % 60;
  // Convert seconds to string
  sprintf(buffer, "Timer:%02u:%02u", minutes, seconds);
  LCD_ShowString(2, 1, buffer);
}

void Display_RunningMessage(uint16_t elapsed_seconds, float loadEff,
                            float voltage) {
  buffer[0] = '\0';
  uint8_t minutes = elapsed_seconds / 60;
  uint8_t seconds = elapsed_seconds % 60;
  // Convert seconds to string
  sprintf(buffer, "%02u:%02u", minutes, seconds);

  // 显示负载效率
  char eff_buffer[16];
  sprintf(eff_buffer, "Eff:%.1f", loadEff);
  LCD_ShowString(1, 1, eff_buffer);

  // 显示电压
  char volt_buffer[16];
  sprintf(volt_buffer, "V:%.2f", voltage);
  LCD_ShowString(1, 8, volt_buffer);

  LCD_ShowString(2, 1, buffer);
}

void Display_IdleMessage(void) {
  LCD_ShowString(1, 1, "System Idle");
  LCD_ShowString(2, 1, "Set Timer");
}

void Display_ErrorMessage(char *message) {
  LCD_ShowString(1, 1, "Error:");
  LCD_ShowString(2, 1, message);
}

void Display_Clear(void) {
  LCD_ShowString(1, 1, "                "); // Clear line 1
  LCD_ShowString(2, 1, "                "); // Clear line 2
}

void Display_LoadingMessage(void) { LCD_ShowString(1, 1, "Loading..."); }

void Display_CalcLoadEfficiency(float efficiency) {
  buffer[0] = '\0';
  // Convert efficiency to string with 2 decimal places
  sprintf(buffer, "Eff: %.2f%%", efficiency);
  LCD_ShowString(2, 1, buffer);
}