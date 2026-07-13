#include "fakeload.h"
// #include <cmath>
#include <math.h>
// #include <stdint.h>

/*
20ohm - 20w
10ohm - 10w
100ohm - 5w
*/

/*
并联电阻组合数组
['P0: 10Ω,P1: 20Ω,P2: 20Ω,P3: 20Ω,P4: 20Ω,P5: 100Ω,',
 'P1: 20Ω,P2: 20Ω,P3: 20Ω,P4: 20Ω,P5: 100Ω,',
  'P0: 10Ω,P2: 20Ω,P3: 20Ω,P4: 20Ω,P5: 100Ω,',
   'P2: 20Ω,P3: 20Ω,P4: 20Ω,P5: 100Ω,',
   'P0: 10Ω,P1: 20Ω,P3: 20Ω,P4: 20Ω,P5: 100Ω,',
    'P1: 20Ω,P3: 20Ω,P4: 20Ω,P5: 100Ω,',
    'P0: 10Ω,P3: 20Ω,P4: 20Ω,P5: 100Ω,',
    'P3: 20Ω,P4: 20Ω,P5: 100Ω,',
     'P0: 10Ω,P1: 20Ω,P2: 20Ω,P4: 20Ω,P5: 100Ω,',
     'P1: 20Ω,P2: 20Ω,P4: 20Ω,P5: 100Ω,',
      'P0: 10Ω,P2: 20Ω,P4: 20Ω,P5: 100Ω,',
      'P2: 20Ω,P4: 20Ω,P5: 100Ω,',
       'P0: 10Ω,P1: 20Ω,P4: 20Ω,P5: 100Ω,',
        'P1: 20Ω,P4: 20Ω,P5: 100Ω,',
        'P0: 10Ω,P4: 20Ω,P5: 100Ω,',
        'P4: 20Ω,P5: 100Ω,',
        'P0: 10Ω,P1: 20Ω,P2: 20Ω,P3: 20Ω,P5: 100Ω,',
         'P1: 20Ω,P2: 20Ω,P3: 20Ω,P5: 100Ω,',
          'P0: 10Ω,P2: 20Ω,P3: 20Ω,P5: 100Ω,',
          'P2: 20Ω,P3: 20Ω,P5: 100Ω,',
          'P0: 10Ω,P1: 20Ω,P3: 20Ω,P5: 100Ω,',
          'P1: 20Ω,P3: 20Ω,P5: 100Ω,',
          'P0: 10Ω,P3: 20Ω,P5: 100Ω,',
          'P3: 20Ω,P5: 100Ω,',
          'P0: 10Ω,P1: 20Ω,P2: 20Ω,P5: 100Ω,',
           'P1: 20Ω,P2: 20Ω,P5: 100Ω,',
           'P0: 10Ω,P2: 20Ω,P5: 100Ω,',
           'P2: 20Ω,P5: 100Ω,',
            'P0: 10Ω,P1: 20Ω,P5: 100Ω,',
            'P1: 20Ω,P5: 100Ω,',
            'P0: 10Ω,P5: 100Ω,',
            'P5: 100Ω,',
            'P0: 10Ω,P1: 20Ω,P2: 20Ω,P3: 20Ω,P4: 20Ω,',
            'P1: 20Ω,P2: 20Ω,P3: 20Ω,P4: 20Ω,',
             'P0: 10Ω,P2: 20Ω,P3: 20Ω,P4: 20Ω,',
             'P2: 20Ω,P3: 20Ω,P4: 20Ω,',
             'P0: 10Ω,P1: 20Ω,P3: 20Ω,P4: 20Ω,',
             'P1: 20Ω,P3: 20Ω,P4: 20Ω,',
              'P0: 10Ω,P3: 20Ω,P4: 20Ω,',
              'P3: 20Ω,P4: 20Ω,',
               'P0: 10Ω,P1: 20Ω,P2: 20Ω,P4: 20Ω,',
               'P1: 20Ω,P2: 20Ω,P4: 20Ω,',
               'P0: 10Ω,P2: 20Ω,P4: 20Ω,',
               'P2: 20Ω,P4: 20Ω,',
               'P0: 10Ω,P1: 20Ω,P4: 20Ω,',
               'P1: 20Ω,P4: 20Ω,',
                'P0: 10Ω,P4: 20Ω,',
                'P4: 20Ω,',
                'P0: 10Ω,P1: 20Ω,P2: 20Ω,P3: 20Ω,',
                 'P1: 20Ω,P2: 20Ω,P3: 20Ω,',
                  'P0: 10Ω,P2: 20Ω,P3: 20Ω,',
                  'P2: 20Ω,P3: 20Ω,',
                   'P0: 10Ω,P1: 20Ω,P3: 20Ω,',
                   'P1: 20Ω,P3: 20Ω,',
                   'P0: 10Ω,P3: 20Ω,',
                   'P3: 20Ω,',
                   'P0: 10Ω,P1: 20Ω,P2: 20Ω,',
                   'P1: 20Ω,P2: 20Ω,',
                   'P0: 10Ω,P2: 20Ω,',
                   'P2: 20Ω,',
                   'P0: 10Ω,P1: 20Ω,',
                   'P1: 20Ω,',
                    'P0: 10Ω,', '']
*/

// float idata resistance_list[RESISTANCE_LIST_SIZE] = {
//     3.23f, 4.76f,  3.85f, 6.25f,  3.85f, 6.25f,  4.76f, 9.09f,  3.85f, 6.25f,
//     4.76f, 9.09f,  4.76f, 9.09f,  6.25f, 16.67f, 3.85f, 6.25f,  4.76f, 9.09f,
//     4.76f, 9.09f,  6.25f, 16.67f, 4.76f, 9.09f,  6.25f, 16.67f, 6.25f, 16.67f,
//     9.09f, 100.0f, 3.33f, 5.0f,   4.0f,  6.67f,  4.0f,  6.67f,  5.0f,  10.0f,
//     4.0f,  6.67f,  5.0f,  10.0f,  5.0f,  10.0f,  6.67f, 20.0f,  4.0f,  6.67f,
//     5.0f,  10.0f,  5.0f,  10.0f,  6.67f, 20.0f,  5.0f,  10.0f,  6.67f, 20.0f,
//     6.67f, 20.0f,  10.0f, 0.0f};
// uint8_t idata resistance_list_hex_list[RESISTANCE_LIST_SIZE] = {
//     0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
//     0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
//     0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0x33, 0x34, 0x35, 0x36,
//     0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42,
//     0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E,
//     0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56};

/*修正*/
// float idata resistance_list[RESISTANCE_LIST_SIZE] = {
//     3.23f, 4.76f,  3.85f,  6.25f,  3.85f, 6.25f,  4.76f, 9.09f,  3.85f, 6.25f,
//     4.76f, 9.09f,  4.76f,  9.09f,  6.25f, 16.67f, 3.85f, 6.25f,  4.76f, 9.09f,
//     4.76f, 9.09f,  6.25f,  16.67f, 4.76f, 9.09f,  6.25f, 16.67f, 6.25f, 16.67f,
//     9.09f,
//     100.0f, 3.33f,  5.00f,  4.00f, 6.67f,  4.00f, 6.67f,  5.00f, 10.00f,
//     4.00f, 6.67f,  5.00f,  10.00f, 5.00f, 10.00f, 6.67f, 20.00f, 4.00f, 6.67f,
//     5.00f, 10.00f, 5.00f,  10.00f, 6.67f, 20.00f, 5.00f, 10.00f, 6.67f, 20.00f,
//     6.67f, 20.00f, 10.00f, 1e9f};
// uint8_t idata resistance_list_hex_list[RESISTANCE_LIST_SIZE] = {
//     0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
//     0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5,
//     0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0,
//     0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB,
//     0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
//     0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

/* 安全阈值简化后 */

// float xdata resistance_list[RESISTANCE_LIST_SIZE] = {
//     3.23f, 3.33f, 3.85f,  4.00f,  4.76f,  5.00f, 6.25f,
//     6.67f, 9.09f, 10.00f, 16.67f, 20.00f, 100.0f};
// uint8_t xdata resistance_list_hex_list[RESISTANCE_LIST_SIZE] = {
//     0xC0, // 3.23
//     0xE0, // 3.33
//     0xC2, // 3.85
//     0xE2, // 4.00
//     0xC1, // 4.76
//     0xE1, // 5.00
//     0xC3, // 6.25
//     0xE3, // 6.67
//     0xC7, // 9.09
//     0xE7, // 10.00
//     0xCF, // 16.67
//     0xEF, // 20.00
//     0xDF  // 100.0
// };
const float xdata resistance_list[RESISTANCE_LIST_SIZE] = {
    3.23f, 3.33f, 3.85f,  4.00f,  4.76f,  5.00f, 6.25f,
    6.67f, 9.09f, 10.00f, 16.67f, 20.00f, 100.0f};

// 对应PCF8574控制码（低电平接入电阻，P6/P7保持高电平）
const uint8_t xdata resistance_list_hex_list[RESISTANCE_LIST_SIZE] = {
    0x3F, // 3.23Ω  全并联
    0x1F, // 3.33Ω  R10 + 4×20Ω
    0x2F, // 3.85Ω  R10 + 3×20Ω + R100
    0x0F, // 4.00Ω  R10 + 3×20Ω
    0x3E, // 4.76Ω  4×20Ω + R100  【最优功率84W】
    0x1E, // 5.00Ω  4×20Ω        【最优功率80W】
    0x2E, // 6.25Ω  3×20Ω + R100 【最优功率64W】
    0x0E, // 6.67Ω  3×20Ω        【最优功率60W】
    0x26, // 9.09Ω  2×20Ω + R100 【最优功率44W】
    0x06, // 10.00Ω 2×20Ω        【最优功率40W】
    0x22, // 16.67Ω 1×20Ω + R100
    0x02, // 20.00Ω 单只20Ω
    0x20  // 100.0Ω 单只R100
};

// 初始化假负载
void FakeLoad_Init(void) {
  // UART_SendString("[DEBUG] FakeLoad_Init: Delay start\r\n");
  Delay_ms(50);
  // UART_SendString(
  // "[DEBUG] FakeLoad_Init: Delay done, calling PCF8574_Init\r\n");
  PCF8574_Init();
  // UART_SendString(
  // "[DEBUG] FakeLoad_Init: PCF8574_Init done, calling FakeLoadTest\r\n");
  // FakeLoadTest();
  // UART_SendString("[DEBUG] FakeLoad_Init: FakeLoadTest done\r\n");
  FakeLoad_Reset();
}

// 由指定目标功率计算并设置电阻负载状态 返回索引
uint8_t FakeLoad_SetPower(float power, float voltage) {
  float xdata target_r;
  float xdata min_diff = 999.0f;
  uint8_t xdata best_index = 0;
  uint8_t xdata i;
  float xdata diff;

  if (power == 0) {
    PCF8574_Write(0xC0);
    return 0;
  }
  target_r = (voltage * voltage) / power;

  for (i = 0; i < RESISTANCE_LIST_SIZE; i++) {
    diff = fabs(resistance_list[i] - target_r);

    if (diff < min_diff) {
      min_diff = diff;
      best_index = i;
    }
  }

  PCF8574_Write(resistance_list_hex_list[best_index]);

  return best_index;
}

float FakeLoad_getPower(uint8_t resistance_index, float voltage) {
  // 计算功率 P = V^2 / R
  if (resistance_list[resistance_index] == 0.0f) {
    return 0.0f;
  }
  return voltage * voltage / resistance_list[resistance_index];
}

void FakeLoad_SetResistance(uint8_t resistance_index) {
  PCF8574_Write(resistance_list_hex_list[resistance_index]);
}

float FakeLoad_getResistance(uint8_t resistance_index) {
  return resistance_list[resistance_index];
}
void FakeLoad_Reset(void) { PCF8574_Write(0x00); }
void FakeLoad_Set(uint8_t port, uint8_t value) { PCF8574_SetPort(port, value); }

void FakeLoadTest(void) {
  uint8_t test_index = 0;

  for (test_index = 0; test_index < 7; test_index++) {
    FakeLoad_Set(test_index, 1);
    Delay_ms(50);
  }
  for (test_index = 0; test_index < 7; test_index++) {
    FakeLoad_Set(test_index, 0);
    Delay_ms(50);
  }
}