#include "Display.h"
#include "LCD1602.h"
// Keil C51 does not support stdint.h, using native types

extern bit idata voltage_valid; // 电压数据是否有效（来自main.c）
static char idata buffer[16];

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
  // 使用未调用的LCD_ShowChar和LCD_ShowNum
  LCD_ShowChar(1, 16, 'v');
  LCD_ShowNum(1, 15, 1, 1);
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
  // 不调用LCD_Clear()，直接用空格覆盖旧内容后写入新内容，避免闪烁
  sprintf(buffer, "Power: %.2fW  ", power);
  LCD_ShowString(1, 1, buffer);
  // 下方显示电阻和电压（缩短格式以适应16字符限制）
  if (voltage_valid) {
    if (voltage > 9.9) {
      sprintf(buffer, "R:%.2fOhm %.1fV ", resistance, voltage);
    } else {
      sprintf(buffer, "R:%.2fOhm %.2fV ", resistance, voltage);
    }
  } else {
    sprintf(buffer, "R:%.2fOhm V:UK  ", resistance);
  }
  LCD_ShowString(2, 1, buffer);
}

// Timer: 分钟:秒（Stand模式显示"Timer:    Stand"）
void Display_TimerSetupMessage(uint16_t set_seconds, bit is_stand_mode) {
  // 1602模拟显示效果 (示例 set_seconds=125 -> 2分05秒):
  // [                ]
  // [Timer:02:05     ]
  // 或Stand模式:
  // [                ]
  // [Timer:    Stand ]
  buffer[0] = '\0';
  LCD_Clear();
  if (is_stand_mode) {
    LCD_ShowString(2, 1, "Timer:    Stand");
  } else {
    uint8_t xdata minutes = set_seconds / 60;
    uint8_t xdata seconds = set_seconds % 60;
    LCD_ShowString(2, 1, "Timer:");
    LCD_ShowNum(2, 7, minutes, 2);
    LCD_ShowChar(2, 9, ':');
    LCD_ShowNum(2, 10, seconds, 2);
  }
}

void Display_RunningMessage(uint16_t elapsed_seconds, float loadEff,
                            float voltage, bit is_free_run) {
  // 1602模拟显示效果 (示例 elapsed=45, power=85.5, voltage=12.34):
  // [P:85.5W V:12.34]
  // [00:45 Running   ]
  // 或自由运行模式:
  // [P:85.5W V:12.34]
  // [F 00:45 Running ]

  unsigned int xdata minutes = elapsed_seconds / 60;
  unsigned int xdata seconds = elapsed_seconds % 60;
  char xdata pwr_buffer[16];
  char xdata volt_buffer[16];

  LCD_Clear();
  buffer[0] = '\0';
  // 用空格覆盖第一行
  LCD_ShowString(1, 1, "                ");
  // 倒计时/正计时显示（自由运行模式加F前缀）
  if (is_free_run) {
    sprintf(buffer, "F %02u:%02u Running", minutes, seconds);
  } else {
    sprintf(buffer, "%02u:%02u Running", minutes, seconds);
  }

  // 显示功率（统一标签与设置界面一致）
  sprintf(pwr_buffer, "P:%.1fW ", loadEff);
  LCD_ShowString(1, 1, pwr_buffer);

  // 显示电压（无效时显示V:UK）
  if (voltage_valid) {
    sprintf(volt_buffer, " V:%.2f ", voltage);
  } else {
    sprintf(volt_buffer, " V:UK   ");
  }
  LCD_ShowString(1, 8, volt_buffer);

  // 用空格覆盖第二行
  LCD_ShowString(2, 1, "                ");
  LCD_ShowString(2, 1, buffer);
}

void Display_IdleMessage(void) {
  // 1602模拟显示效果:
  // [System Idle     ]
  // [Set Timer       ]
  LCD_Clear();
  LCD_ShowString(1, 1, "System Idle");
  LCD_ShowString(2, 1, "Set Timer");
}

void Display_ErrorMessage(char *message) {
  // 1602模拟显示效果 (示例 message="Overload"):
  // [Error:          ]
  // [Overload        ]
  LCD_Clear();
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