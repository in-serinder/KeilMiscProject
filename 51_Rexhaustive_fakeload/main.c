
#include "Delay.h"
#include "LCD1602.h"
#include "fakeload.h"
#include "key.h"
#include "peripheral.h"
#include "stc89c52.h"
#include <stdint.h>

// 编码器旋转只去变动一个功耗索引数值
uint8_t loadIndex = 0;
uint16_t duration_time_seconds = 0;
float idata voltage = 0.0f;

// 调整值
bit is_adjusting_duration_time = 0;
bit is_running = 0;

void main(void) {
  Display_Init();
  Display_BootMessage();
  FakeLoad_Init();
  EC11_Init();
  Key_Init();
  Display_Init();

  shellFAN(1);
  extFAN(1);
  ADC_Init();

  Display_IdleMessage();

  while (1) {
    if (is_running) {
    }
    FakeLoad_SetPower(loadIndex);
  }
}
// 编码器方向/按键状态回调函数
void encode_CallBack(bit dir, bit keystate) {
  if (keystate == 0) {
    is_adjusting_duration_time = !is_adjusting_duration_time;
  }
  //
  if (!is_adjusting_duration_time) {
    // 编码器
    if (dir == 0) {
      // A->B
      loadIndex = (loadIndex >= 0) ? 0 : loadIndex + 1;
    } else {
      // B->A
      loadIndex = (loadIndex >= 0) ? 0 : loadIndex - 1;
    }
  } else {
    if (dir == 0) {
      // A->B
      duration_time_seconds =
          (duration_time_seconds >= 0) ? 0 : duration_time_seconds + 1;
    } else {
      // B->A
      duration_time_seconds =
          (duration_time_seconds >= 0) ? 0 : duration_time_seconds - 1;
    }
    Display_TimerSetupMessage(duration_time_seconds);
  }
}

void key_CallBack(bit keystate) {
  if (keystate == 0) {
    is_running = !is_running;
    runStateLED(is_running);
  }
}
