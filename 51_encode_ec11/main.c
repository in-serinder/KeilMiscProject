#include "encode.h"
#include "stc89c52.h"

void main(void) {
  EC11_Init();
  P1 = 0x01;
  while (1)
    ;
}

// dir-编码器方向触发 state-编码器按下触发 0: A->B 1: B->A>
void encode_CallBack(bit dir, bit state) {

  if (state == 0) {
    // 首要判断是否为AB移动
    if (dir == 0)
      P1 <<= 1;
    else
      P1 >>= 1;
  } else {
    // 按键触发
    P1 = 0x0F;
  }
}
