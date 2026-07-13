#include "peripheral.h"
#include "Delay.h"
#include "Timer.h"
#include <intrins.h>

uint16_t PWMPeriod = 0;
uint16_t pwmCounter = 0;
uint8_t dutyCycle = 0;
bit buzzer_en = 0;                    // 蜂鸣总开关
volatile uint16_t idata sys_tick_1ms; // 1ms硬件滴答计数器（在main中初始化为0）
#define BUZZER_VOLUME 50              // 默认音量50%占空比
#define TICK_PER_SEC 250              // 全局宏，解决未定义报错

#define Buzzer P36
#define ShellFAN P25
#define EXTFAN P24
#define HEARTFAN P34
#define RUNSTATE_LED P35

// 编码器相关（来自encode.c，Timer0 ISR中采样）
extern bit AB_DIR;
extern bit enc_event_flag;
extern bit enc_event_dir;
extern bit enc_event_key;
extern uint8_t idata enc_int_count;

// 系统滴答计数器（1ms递增，用于精确计时）
volatile uint16_t idata sys_tick_1ms = 0;

/*功能函数*/
void Timer0_ISR(void) interrupt 1 {
  static uint8_t idata enc_prev_a = 1;
  uint8_t idata enc_a;

  TH0 = 0xFC;
  TL0 = 0x66;

  sys_tick_1ms++; // 1ms系统滴答

  // === 编码器A相采样（1ms轮询，替代INT0中断） ===
  enc_a = P32; // EC11_A
  if (enc_a == 0 && enc_prev_a == 1) {
    // 检测到下降沿，读取B相判断方向
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    AB_DIR = (P12 == 0) ? 0 : 1; // EC11_B
    enc_event_flag = 1;
    enc_event_dir = AB_DIR;
    enc_event_key = 0;
    enc_int_count++;
  }
  enc_prev_a = enc_a;

  // === 蜂鸣器PWM ===
  if (!buzzer_en) {
    Buzzer = 0;
    return;
  }

  pwmCounter++;
  if (pwmCounter >= PWMPeriod) {
    pwmCounter = 0;
  }

  Buzzer = (pwmCounter < dutyCycle) ? 1 : 0;
}

void setBuzzerDutyCycle(uint8_t duty) { dutyCycle = (duty > 100) ? 100 : duty; }

/*外设函数*/
// freq=0 关闭蜂鸣；freq>0 开启对应频率发声
void BuzzerPWM(uint8_t freq) {
  if (freq == 0) {
    buzzer_en = 0;
    PWMPeriod = 0;
    Buzzer = 0;
    return;
  }

  buzzer_en = 1;
  setBuzzerDutyCycle(BUZZER_VOLUME); // 开启默认音量

  // PWM周期计算
  PWMPeriod = TICK_PER_SEC / freq;

  // 限制周期范围 1~200
  if (PWMPeriod < 1)
    PWMPeriod = 1;
  if (PWMPeriod > 200)
    PWMPeriod = 200;
}

void shellFAN(bit state) { ShellFAN = state; }

void extFAN(bit state) { EXTFAN = state; }

void heartFAN(bit state) { HEARTFAN = state; }

void runStateLED(bit state) { RUNSTATE_LED = state; }