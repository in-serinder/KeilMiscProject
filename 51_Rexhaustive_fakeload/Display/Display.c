#include "Display.h"
#include "LCD1602.h"
// Keil C51 does not support stdint.h, using native types

char idata buffer[16];

void Display_Init(void) {
  // Initialization code for the display
  // 1602模拟显示效果:
  // [                ]
  // [                ]
  LCD_Init();
}

void Display_BootMessage(void) {
  // 1602模拟显示效果:
  // [FakeLoad Boot   ]
  // [Initializing... ]
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

void Display_FakeLoad(float power, float resistance, float voltage) {
  // 1602模拟显示效果 (示例 power=15.50, resistance=10.00, voltage=12.00):
  // [Power: 15.50W   ]
  // [R:10.00Ohm V:12.0]
  buffer[0] = '\0';
  sprintf(buffer, "Power: %.2fW", power);
  LCD_ShowString(1, 1, buffer);
  // 下方显示电阻和电压（缩短格式以适应16字符限制）
  sprintf(buffer, "R:%.2fOhm V:%.1f", resistance, voltage);
  LCD_ShowString(2, 1, buffer);
}

// Timer: 分钟:秒
void Display_TimerSetupMessage(uint16_t set_seconds) {
  // 1602模拟显示效果 (示例 set_seconds=125 -> 2分05秒):
  // [                ]
  // [Timer:02:05     ]
  uint8_t xdata minutes = set_seconds / 60;
  uint8_t xdata seconds = set_seconds % 60;
  buffer[0] = '\0';

  // Convert seconds to string
  sprintf(buffer, "Timer:%02u:%02u", minutes, seconds);
  LCD_ShowString(2, 1, buffer);
}

void Display_RunningMessage(uint16_t elapsed_seconds, float loadEff,
                            float voltage) {
  // 1602模拟显示效果 (示例 elapsed=45, eff=85.5, voltage=12.34):
  // [Ef:85.5W V:12.34]
  // [00:45 Running   ]

  uint8_t xdata minutes = elapsed_seconds / 60;
  uint8_t xdata seconds = elapsed_seconds % 60;
  char xdata eff_buffer[16];
  char xdata volt_buffer[16];

  buffer[0] = '\0';

  // Convert seconds to string
  sprintf(buffer, "%02u:%02u Running", minutes, seconds);

  // 显示负载效率

  sprintf(eff_buffer, "Ef:%.1fW", loadEff);
  LCD_ShowString(1, 1, eff_buffer);

  // 显示电压
  sprintf(volt_buffer, "V:%.2f", voltage);
  LCD_ShowString(1, 8, volt_buffer);

  LCD_ShowString(2, 1, buffer);
}

void Display_IdleMessage(void) {
  // 1602模拟显示效果:
  // [System Idle     ]
  // [Set Timer       ]
  LCD_ShowString(1, 1, "System Idle");
  LCD_ShowString(2, 1, "Set Timer");
}

void Display_ErrorMessage(char *message) {
  // 1602模拟显示效果 (示例 message="Overload"):
  // [Error:          ]
  // [Overload        ]
  LCD_ShowString(1, 1, "Error:");
  LCD_ShowString(2, 1, message);
}

// void Display_Clear(void) {
//   // 1602模拟显示效果:
//   // [                ]
//   // [                ]
//   LCD_ShowString(1, 1, "                "); // Clear line 1
//   LCD_ShowString(2, 1, "                "); // Clear line 2
// }

// void Display_LoadingMessage(void) {
//   // 1602模拟显示效果:
//   // [Loading...      ]
//   // [                ]
//   LCD_ShowString(1, 1, "Loading...");
// }

// void Display_CalcLoadEfficiency(float efficiency) {
//   // 1602模拟显示效果 (示例 efficiency=95.67):
//   // [                ]
//   // [Eff: 95.67%     ]
//   buffer[0] = '\0';
//   // Convert efficiency to string with 2 decimal places
//   sprintf(buffer, "Eff: %.2f%%", efficiency);
//   LCD_ShowString(2, 1, buffer);
// }