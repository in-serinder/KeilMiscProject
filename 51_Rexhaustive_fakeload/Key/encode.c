#include "encode.h"

#define EC11_A P32
#define EC11_B P12
#define EC11_KEY P33

// 高电平有效
bit AB_DIR; // 0: 顺时针  1: 逆时针

// 编码器事件标志和数据（Timer0 1ms ISR设置，主循环读取）
bit enc_event_flag = 0;
bit enc_event_dir = 0;
bit enc_event_key = 0;

// 调试用：触发计数器
uint8_t idata enc_int_count = 0;

// 编码器初始化：仅配置IO口，不再使能INT0中断
// 编码器采样由Timer0 1ms定时中断完成（见peripheral.c）
void EC11_Init(void) {
  EC11_A = 1;
  EC11_B = 1;
  EC11_KEY = 1;

  // 关闭INT0中断，改用Timer0定时采样
  EX0 = 0;
  EX1 = 0;
}

// 获取编码器事件（在主循环中调用）
bit EC11_GetEvent(unsigned char *dir, unsigned char *key) {
  if (enc_event_flag) {
    *dir = (unsigned char)enc_event_dir;
    *key = (unsigned char)enc_event_key;
    enc_event_flag = 0;
    return 1;
  }
  return 0;
}

// 按键非阻塞扫描（不阻塞主循环）
bit EC11_ScanKey(void) {
  static bit idata key_pressed = 0;      // 按键按下标志
  static uint8_t idata key_debounce = 0; // 防抖计数器

  if (EC11_KEY == 0) {
    // 按键按下，启动防抖计数
    if (!key_pressed) {
      if (key_debounce < 2) {
        key_debounce++; // 约20ms防抖（10ms一次调用）
      } else {
        // 防抖完成，确认按下
        key_pressed = 1;
        key_debounce = 0;
        enc_event_flag = 1;
        enc_event_dir = AB_DIR;
        enc_event_key = 1; // 1=按键按下
        return 1;
      }
    }
  } else {
    // 按键松开，清除标志
    key_pressed = 0;
    key_debounce = 0;
  }
  return 0;
}

// bit checkDirection(void) { return AB_DIR; } // 未使用