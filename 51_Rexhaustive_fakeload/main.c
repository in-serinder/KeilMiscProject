
#include "Delay.h"
#include "LCD1602.h"
#include "fakeload.h"
#include "key.h"
#include "peripheral.h"
#include "stc89c52.h"

// 编码器旋转只去变动一个功耗索引数值
uint8_t loadIndex = 0;

void main(void) {
  Display_Init();
  Display_BootMessage();
  FakeLoad_Init();
  EC11_Init();
  Key_Init();
  Display_Init();

  while (1) {
  }
}
// 编码器方向/按键状态回调函数
void encode_CallBack(bit dir, bit keystate) {
  //
  if (keystate != 0) {
    // 编码器
    if (dir == 0) {
      // A->B
    } else {
      // B->A
    }
  }
}

void key_CallBack(bit keystate) {}
