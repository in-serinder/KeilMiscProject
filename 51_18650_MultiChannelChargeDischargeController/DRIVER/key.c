#include "key.h"

#define THRESHOLD_ADD P33 // INT1 加阈值
#define THRESHOLD_SUB P32 // INT0 减阈值

uint8_t threshold = 0; // 充电百分比阈值
// 按键事件标志：0无事件，1减键触发，2加键触发
uint8_t key_event_flag = 0;
uint16_t key_long_tick = 0; // 长按计时
uint8_t key_press=0;

void KEY_Init(void)
{
    // 设置P32和P33为输入模式
    P3M0 &= ~(0x0C);  // P32、P33设置为高阻输入
    P3M1 |= 0x0C;
    
    // 配置外部中断触发方式为下降沿触发
    IT0 = 1;  // INT0下降沿触发
    IT1 = 1;  // INT1下降沿触发
    
    // 使能外部中断
    EX0 = 1;  // 使能INT0
    EX1 = 1;  // 使能INT1
    
    // 开总中断
    EA = 1;
}
/**
 * @brief 减阈值按键中断 INT0(P32)
 */
void EXTI0_IRQHandler(void) interrupt 0 { key_event_flag = 1; }

/**
 * @brief 加阈值按键中断 INT1(P33)
 */
void EXTI1_IRQHandler(void) interrupt 2 { key_event_flag = 2; }

void KEY_Scan_Handle(void) {
  if (key_event_flag == 0) {
    key_long_tick = 0;
    return;
  }

  delay_ms(10);
  key_press = 0;
  if (key_event_flag == 1 && THRESHOLD_SUB == 0)
    key_press = 1;
  if (key_event_flag == 2 && THRESHOLD_ADD == 0)
    key_press = 2;

  if (key_press == 0) {
    key_event_flag = 0;
    key_long_tick = 0;
    return;
  }

  // 短按单次增减
  if (key_long_tick == 0) {
    if (key_press == 1 && threshold > 0)
      threshold--;
    if (key_press == 2 && threshold < 100)
      threshold++;
  }
  key_long_tick++;

  // 长按500ms后快速连续增减
  if (key_long_tick >= 50) {
    if (key_press == 1 && threshold > 0)
      threshold--;
    if (key_press == 2 && threshold < 100)
      threshold++;
    delay_ms(50); // 长按步进间隔
  }
}

uint8_t KEY_GetThreshold(void) { return threshold; }
