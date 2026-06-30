#include "Delay.h"
#include "LCD1602.h"
#include "TLCx543.h"
#include "fakeload.h"
#include "key.h"
#include "peripheral.h"
#include "stc89c52.h"
#include <stdint.h>

// 编码器旋转只去变动一个功耗索引数值
uint8_t loadIndex = 0;
uint16_t duration_time_seconds = 0;
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
  TLCx543_Init();
  FakeLoad_Reset();
  // 预采集修复
  voltage = TLCx543_ReadVoltageV(0);

  // 启用风扇
  shellFAN(1);
  extFAN(1);

  // 显示空闲
  Display_IdleMessage();

  while (1) {
    if (is_running) {
    }
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
    Display_FakeLoad(power);
    // FakeLoad_SetPower(loadIndex);
    FakeLoad_SetResistance(loadIndex);
  }
}