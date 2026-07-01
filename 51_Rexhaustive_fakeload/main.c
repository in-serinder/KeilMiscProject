#include "Delay.h"
#include "Display.h"
#include "LCD1602.h"
#include "TLCx543.h"
#include "encode.h"
#include "fakeload.h"
#include "key.h"
#include "peripheral.h"
#include "stc89c52.h"
// Keil C51 does not support stdint.h, using native types
// void vector_lock(void) code_at 0x0000 {
// goto main;             // 0000 复位
// goto EC11_A_Triggered; // 0003 INT0
// goto Timer0_ISR;       // 000B T0 关键保护区域
//  ;                      // 填充剩余字节
//}
#define BEEP_SEC 3U
void FakeLoadTest(void);
// 编码器旋转只去变动一个功耗索引数值
uint8_t loadIndex = 0;
uint16_t idata tick = 0; // 10ms -》 1tick
uint16_t idata buzzer_tick = 0;
uint16_t idata duration_time_seconds = 0;
float idata voltage = 0.0f;
float idata power = 0.0f;

// 调整值
bit is_adjusting_duration_time =
    0; // 是否正在调整时间 0:调整功率负载 1:调整持续时间
bit is_running = 0;

void main(void) {
  Display_Init();
  // 初始化显示
  Display_BootMessage();
  FakeLoad_Init();
  EC11_Init();
  Key_Init();
  Display_Init();
  TLCx543_Init(TLC1543);
  FakeLoad_Reset();
  FakeLoadTest();
  // 预采集修复
  voltage = TLCx543_ReadVoltageV(6);

  // 启用风扇
  shellFAN(1);
  extFAN(1);

  // 显示空闲
  Display_IdleMessage();

  while (1) {
    // 10ms基础节拍必执行
    voltage = (TLCx543_ReadADC(6) * 5.0f / 1024.0f) * 34700.0f / 4700.0f;

    // 按键防抖扫描
    if (Key_Scan() == 0) {
      Delay_ms(20);
      while (Key_Scan() == 0)
        ;
      is_running = !is_running;
      buzzer_tick = 0; // 按键启停直接关闭结束蜂鸣
    }

    if (is_running) {
      heartFAN(1);
      FakeLoad_SetResistance(loadIndex);
      power = FakeLoad_getPower(loadIndex, voltage);

      if (tick >= 100) // 1秒周期刷新
      {
        Display_RunningMessage(duration_time_seconds, power, voltage);
        if (duration_time_seconds > 0) {
          duration_time_seconds--;
          // 计时归零触发蜂鸣
          if (duration_time_seconds == 0) {
            buzzer_tick = BEEP_SEC * 100; // 3s = 300个10ms节拍
          }
        }
        tick = 0;
      }
      tick++;
    } else {
      heartFAN(0);
      FakeLoad_Reset();
      tick = 0;
      buzzer_tick = 0; // 停止负载直接关蜂鸣
    }

    // 蜂鸣器逻辑
    if (buzzer_tick > 0) {
      BuzzerPWM(200); // 鸣叫频率200，可自行调整音调
      buzzer_tick--;
    } else {
      BuzzerPWM(0); // 关闭蜂鸣
    }

    Delay_ms(10);
  }
}
// 编码器方向/按键状态回调函数
void encode_CallBack(bit dir, bit keystate) {
  // 先决定反转状态
  if (keystate == 0) {
    is_adjusting_duration_time = !is_adjusting_duration_time;
  }

  if (is_adjusting_duration_time) {
    // 调整持续时间
    if (dir == 0) {
      duration_time_seconds++;
    } else {
      duration_time_seconds--;
    }
    Display_TimerSetupMessage(duration_time_seconds);
  } else {
    // 调整功率负载
    voltage = TLCx543_ReadVoltageV(0);
    /* 索引范围检查 */
    if (dir == 0) {
      if (loadIndex < RESISTANCE_LIST_SIZE - 1) {
        loadIndex++;
      }
    } else {
      if (loadIndex > 0) {
        loadIndex--;
      }
    }
    power = FakeLoad_getPower(loadIndex, voltage);
    Display_FakeLoad(power, FakeLoad_getResistance(loadIndex), voltage);
    // FakeLoad_SetPower(loadIndex);
  }
}

void FakeLoadTest(void) {
  uint8_t test_index = 0;

  for (test_index = 0; test_index < 7; test_index++) {
    FakeLoad_Set(test_index, 1);
    Delay_ms(500);
  }
  for (test_index = 0; test_index < 7; test_index++) {
    FakeLoad_Set(test_index, 0);
    Delay_ms(500);
  }
}