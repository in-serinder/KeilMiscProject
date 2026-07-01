#include "peripheral.h"
#include "Delay.h"
#include "Timer.h"

uint16_t PWMPeriod = 0;
uint16_t pwmCounter = 0;
uint8_t dutyCycle = 0;
bit buzzer_en = 0;       // 蜂鸣总开关
#define BUZZER_VOLUME 50 // 默认音量50%占空比
#define TICK_PER_SEC 250 // 全局宏，解决未定义报错

#define Buzzer P36
#define ShellFAN P25
#define EXTFAN P24
#define HEARTFAN P34
#define RUNSTATE_LED P35

/*功能函数*/
void Timer0_ISR(void) interrupt 1 {
  TH0 = 0xFC;
  TL0 = 0x66;

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