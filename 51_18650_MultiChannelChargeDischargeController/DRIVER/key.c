#include "key.h"

#define THRESHOLD_ADD P33
#define THRESHOLD_SUB P32

uint8_t threshold = 70;
volatile uint8_t key_event_flag = 0; // volatile：中断会修改
uint16_t key_press_tick = 0;
uint16_t key_last_repeat_tick = 0;
bit key_was_pressed = 0;

void KEY_Init(void) {
  P3M0 &= ~(0x0C);
  P3M1 &= ~(0x0C);
  P32 = 1;
  P33 = 1;

  IT0 = 1;
  IT1 = 1;
  EX0 = 1;
  EX1 = 1;
}

void EXTI0_IRQHandler(void) interrupt 0 { key_event_flag = 1; }
void EXTI1_IRQHandler(void) interrupt 2 { key_event_flag = 2; }

/**
 * @brief 按键扫描处理（非阻塞）
 *        所有对 threshold/key_event_flag 的操作都关中断保护
 */
bit KEY_Scan_Handle(uint16_t now_tick) {
  bit changed = 0;
  uint8_t key_down = 0;
  uint8_t flag;

  EA = 0;
  flag = key_event_flag;
  EA = 1;

  // 直接读引脚电平，不依赖flag（长按期间无中断产生）
  if (THRESHOLD_SUB == 0)
    key_down = 1;
  if (THRESHOLD_ADD == 0)
    key_down = 2;

  // 按键未按下
  if (key_down == 0) {
    key_was_pressed = 0;
    if (flag != 0) {
      EA = 0;
      key_event_flag = 0;
      EA = 1;
    }
    return 0;
  }

  // 确认按键后清flag
  if (flag != 0) {
    EA = 0;
    key_event_flag = 0;
    EA = 1;
  }

  if (!key_was_pressed) {
    key_was_pressed = 1;
    key_press_tick = now_tick;
    key_last_repeat_tick = now_tick;

    EA = 0;
    if (key_down == 1 && threshold > 0) {
      threshold--;
      changed = 1;
    }
    if (key_down == 2 && threshold < 100) {
      threshold++;
      changed = 1;
    }
    EA = 1;
    return changed;
  }

  // 长按：按稳300tick后，每150tick重复1次（每次只加减1个值，避免突变）
  if ((now_tick - key_press_tick) >= 300 &&
      (now_tick - key_last_repeat_tick) >= 150) {
    key_last_repeat_tick = now_tick;
    EA = 0;
    if (key_down == 1 && threshold > 0) {
      threshold--;
      changed = 1;
    }
    if (key_down == 2 && threshold < 100) {
      threshold++;
      changed = 1;
    }
    EA = 1;
  }

  return changed;
}

uint8_t KEY_GetThreshold(void) {
  uint8_t val;
  EA = 0;
  val = threshold;
  EA = 1;
  return val;
}