
#include "Delay.h"
#include "LCD1602.h"
#include "fakeload.h"
#include "key.h"
#include "peripheral.h"
#include "stc89c52.h"

void main(void) {
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
