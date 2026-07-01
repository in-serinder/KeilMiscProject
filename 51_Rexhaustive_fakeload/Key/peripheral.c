#include "peripheral.h"
#include "Delay.h"
#include "Timer.h"

uint16_t xdata PWMPeriod = 100; // PWM周期，单位为中断次数
uint16_t xdata pwmCounter = 0;  // PWM计数器
uint8_t xdata dutyCycle = 50;   // 占空比

#define Buzzer P36
#define ShellFAN P25
#define EXTFAN P24
#define HEARTFAN P34
#define RUNSTATE_LED P35

/*功能函数*/

void Timer0_ISR(void) interrupt 1 {
  // 无操作，仅用于产生定时中断
  TH0 = 0xFC;
  TL0 = 0x66;

  // PWM控制逻辑
  pwmCounter++;
  if (pwmCounter >= PWMPeriod) {
    pwmCounter = 0;
  }

  if (pwmCounter < dutyCycle) {
    Buzzer = 1;
  } else {
    Buzzer = 0;
  }
}

void setBuzzerDutyCycle(uint8_t duty) {
  if (duty > 100) {
    duty = 100; // 限制最大占空比为100%
  }
  dutyCycle = duty;
}

/*外设函数*/

void BuzzerPWM(uint8_t freq) {
  Timer0_Init();

  if (freq == 0) {
    PWMPeriod = 0; // 关闭蜂鸣器
  } else {
    PWMPeriod = 1000000 / (freq * 10); // 根据频率计算PWM周期
    PWMPeriod = (PWMPeriod > 200) ? 200
                : (PWMPeriod < 1) ? 1
                                  : PWMPeriod; // 限制PWM周期范围在1到200之间
  }
}

void shellFAN(bit state) { ShellFAN = state; }

void extFAN(bit state) { EXTFAN = state; }

void heartFAN(bit state) { HEARTFAN = state; }

void runStateLED(bit state) { RUNSTATE_LED = state; }