#include "fakeload.h"
#include <math.h>
#include <stdint.h>

#define RESISTANCE_LIST_SIZE 64
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

float idata resistance_list[RESISTANCE_LIST_SIZE] = {
    3.23f, 4.76f,  3.85f, 6.25f,  3.85f, 6.25f,  4.76f, 9.09f,  3.85f, 6.25f,
    4.76f, 9.09f,  4.76f, 9.09f,  6.25f, 16.67f, 3.85f, 6.25f,  4.76f, 9.09f,
    4.76f, 9.09f,  6.25f, 16.67f, 4.76f, 9.09f,  6.25f, 16.67f, 6.25f, 16.67f,
    9.09f, 100.0f, 3.33f, 5.0f,   4.0f,  6.67f,  4.0f,  6.67f,  5.0f,  10.0f,
    4.0f,  6.67f,  5.0f,  10.0f,  5.0f,  10.0f,  6.67f, 20.0f,  4.0f,  6.67f,
    5.0f,  10.0f,  5.0f,  10.0f,  6.67f, 20.0f,  5.0f,  10.0f,  6.67f, 20.0f,
    6.67f, 20.0f,  10.0f, 0.0f};
uint8_t idata resistance_list_hex_list[RESISTANCE_LIST_SIZE] = {
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
    0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xD8, 0xD9, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3,
    0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

// 初始化假负载
void FakeLoad_Init(void) {
  PCF8574_Init();
  // 初始化所有电阻继电器关闭
  PCF8574_SetPort(R10, 0);   // 10R 关闭
  PCF8574_SetPort(R20_1, 0); // 20R_1 关闭
  PCF8574_SetPort(R20_2, 0); // 20R_2 关闭
  PCF8574_SetPort(R20_3, 0); // 20R_3 关闭
  PCF8574_SetPort(R20_4, 0); // 20R_4 关闭
  PCF8574_SetPort(R100, 0);  // 100R 关闭
}

// 由指定目标功率计算并设置电阻负载状态 返回索引
uint8_t FakeLoad_SetPower(float power, float voltage) {
  float target_r;
  float min_diff = 999.0f;
  uint8_t best_index = 0;
  uint8_t i;
  float diff;

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

void FakeLoad_SetResistance(uint8_t resistance_index) {
  PCF8574_Write(resistance_list_hex_list[resistance_index]);
}

void FakeLoad_Reset(void) { PCF8574_Write(0x00); }
void FakeLoad_Set(uint8_t port, uint8_t value) { PCF8574_SetPort(port, value); }
