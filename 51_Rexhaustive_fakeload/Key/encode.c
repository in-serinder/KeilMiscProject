#include "encode.h"

#define EC11_A P32
#define EC11_B P12
#define EC11_KEY P43

// 高电平有效
bit AB_DIR; // 0: A->B  1: B->A>

// AB都在中断上
// void EC11_Init(void) {
//   EC11_A = 1;
//   EC11_B = 1;
//   EC11_KEY = 1;

//   IT0 = 1; // INT0(P3.2)低电平触发
//   EX0 = 1; // 使能INT0中断

//   IT1 = 1; // INT1(P3.3)低电平触发
//   EX1 = 1; // 使能INT1中断

//   IT2 = 1; // INT2(P3.4)低电平触发
//   EX2 = 1; // 使能INT2中断

//   EA = 1; // 使能全局中断
// }

// void disableAint(void) { EX0 = 0; }
// void enableAint(void) { EX0 = 1; }

// void disableBint(void) { EX1 = 0; }
// void enableBint(void) { EX1 = 1; }

// void EC11_A_Triggered(void) interrupt 0 {
//   disableBint();
//   P1 = 0xFF;

//   if (EC11_B == 0)
//     AB_DIR = 0;

//   // 回调到main - state=0表示旋转
//   encode_CallBack(AB_DIR, 0);
//   enableBint();
// }

// void EC11_B_Triggered(void) interrupt 1 {
//   disableAint();
//   if (EC11_A == 0)
//     AB_DIR = 1;
//   encode_CallBack(AB_DIR, 0);
//   enableAint();
// }

// void EC11_KEY_Triggered(void) interrupt 10 {
//   encode_CallBack(AB_DIR, 1); // state=1表示按键
// }

// bit checkDirection(void) { return AB_DIR; }

// 减少中断占用
void EC11_Init(void) {
  EC11_A = 1;
  EC11_B = 1;
  EC11_KEY = 1;

  // 只开INT0中断，INT1留给Timer0蜂鸣，禁止INT1中断
  IT0 = 1; // 下降沿触发（编码器标准）
  EX0 = 1; // 使能INT0
  EX1 = 0; // 关闭INT1外部中断，留给定时器0

  EA = 1; // 全局中断开
}

void disableAint(void) { EX0 = 0; }
void enableAint(void) { EX0 = 1; }

// 仅A相中断，B相普通IO读取判断方向
void EC11_A_Triggered(void) interrupt 0 {
  disableAint();

  if (EC11_B == 0)
    AB_DIR = 0;
  else
    AB_DIR = 1;

  encode_CallBack(AB_DIR, 0); // 0=旋转
  enableAint();
}

// 按键不使用中断，主循环轮询调用
bit EC11_ScanKey(void) {
  if (EC11_KEY == 0) {
    Delay_ms(20); // 防抖
    if (EC11_KEY == 0) {
      while (EC11_KEY == 0)
        ;                         // 等待松手
      encode_CallBack(AB_DIR, 1); // 1=按键按下
      return 1;
    }
  }
  return 0;
}

bit checkDirection(void) { return AB_DIR; }