#include "encode.h"
#include "Delay.h"
#include "main.c"
#include "stc89c52.h"

sbit EC11_A = P32;
sbit EC11_B = P33;
sbit EC11_KEY = P30;
sbit KEY_RUN = P42;

// 电路定义是低电平有效 但这里用了外部中断
bit AB_DIR; // 0: A->B  1: B->A>

// AB都在中断上
void EC11_Init(void) {
  EC11_A = 1;
  EC11_B = 1;
  EC11_KEY = 1;
  KEY_RUN = 1;

  IT0 = 1; // INT0(P3.2)下降沿中断
  EX0 = 1; // 使能INT0中断

  IT1 = 1; // INT1(P3.3)下降沿中断
  EX1 = 1; // 使能INT1中断

  INTCLKO |= 0x10; // 使能INT2中断
}

void Key_Init(void) {
  INTCLKO |= 0x20; // 使能INT3中断
}

void disableAint(void) { EX0 = 0; }
void enableAint(void) { EX0 = 1; }

void disableBint(void) { EX1 = 0; }
void enableBint(void) { EX1 = 1; }

void EC11_A_Triggered(void) interrupt 0 {
  disableBint();
  if (EC11_B == 0)
    AB_DIR = 0;
  // 回调到main
  encode_CallBack(AB_DIR, 1);
}

void EC11_B_Triggered(void) interrupt 1 {
  disableAint();
  if (EC11_A == 0)
    AB_DIR = 1;
  encode_CallBack(AB_DIR, 1);
}

void EC11_KEY_Triggered(void) interrupt 10 { encode_CallBack(AB_DIR, 1); }

void KEY_Running_Triggered(void) interrupt 11 { key_CallBack(0); }

void checkDirection(void) { return AB_DIR; }