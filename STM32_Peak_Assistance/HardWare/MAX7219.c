#include "MAX7219.h"
#include "Delay.h"

#define MAX7219_CLK_PORT GPIOB
#define MAX7219_CLK_PIN GPIO_Pin_9
#define MAX7219_DIN_PORT GPIOB
#define MAX7219_DIN_PIN GPIO_Pin_7
#define MAX7219_CS_PORT GPIOB
#define MAX7219_CS_PIN GPIO_Pin_8

#define MAX7219_CLK_H() GPIO_SetBits(MAX7219_CLK_PORT, MAX7219_CLK_PIN)
#define MAX7219_CLK_L() GPIO_ResetBits(MAX7219_CLK_PORT, MAX7219_CLK_PIN)
#define MAX7219_DIN_H() GPIO_SetBits(MAX7219_DIN_PORT, MAX7219_DIN_PIN)
#define MAX7219_DIN_L() GPIO_ResetBits(MAX7219_DIN_PORT, MAX7219_DIN_PIN)
#define MAX7219_CS_H() GPIO_SetBits(MAX7219_CS_PORT, MAX7219_CS_PIN)
#define MAX7219_CS_L() GPIO_ResetBits(MAX7219_CS_PORT, MAX7219_CS_PIN)

#define MAX7219_BLANK 0x0F

static void MAX7219_SendByte(uint8_t dat) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    // ── CLK 低电平：先稳定 DIN ──
    MAX7219_CLK_L();
    if (dat & 0x80)
      MAX7219_DIN_H();
    else
      MAX7219_DIN_L();
    Delay_us(2); // DIN setup + CLK 低电平（≥25ns要求，这里留2us余量）
    // ── CLK 上升沿：MAX7219 在此采样 DIN ──
    MAX7219_CLK_H();
    Delay_us(2); // CLK 高电平（≥50ns要求，留2us余量）
    dat <<= 1;
  }
}

void MAX7219_Write(uint8_t address, uint8_t dat) {
  MAX7219_CS_L();
  Delay_us(5);               // CS setup 时间
  MAX7219_SendByte(address); // 地址字节（高8位，MAX7219仅用bit3-bit0）
  MAX7219_SendByte(dat);     // 数据字节（低8位）
  Delay_us(5);               // CS hold 时间
  MAX7219_CS_H();
  Delay_us(10); // 两次写入之间留出间隔
}

void MAX7219_ReInit(void) {
  MAX7219_Write(MAX7219_SHUTDOWN, 0x01);
  Delay_ms(1);
  MAX7219_Write(MAX7219_DISPLAYTEST, 0x00);
  Delay_ms(1);
  MAX7219_Write(MAX7219_SCANLIMIT, 0x07);
  Delay_ms(1);
  MAX7219_Write(MAX7219_DECODEMODE, 0xFF);
  Delay_ms(1);
  MAX7219_Write(MAX7219_INTENSITY, 0x08);
  Delay_ms(1);
}

void MAX7219_Init(void) {
  uint8_t i;
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

  GPIO_InitStructure.GPIO_Pin =
      MAX7219_CLK_PIN | MAX7219_DIN_PIN | MAX7219_CS_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOB, &GPIO_InitStructure);

  // ====== 等待 MAX7219 内部 POR（上电复位）完成 ======
  // MAX7219 数据手册：POR 时间典型值 100μs
  // 但实际中第一次上电可能更长（电源上升时间慢），预留 >= 50ms
  // 重要：在 MAX7219 完成内部复位之前，所有 SPI 写入都会被忽略！
  MAX7219_CS_H();
  MAX7219_CLK_L();
  MAX7219_DIN_L();
  Delay_ms(50);

  // ====== 发送 NOP 脉冲清除移位寄存器 ======
  // 在 CS=H 时发送 CLK 脉冲，MAX7219 不会锁存数据
  // 目的是清除 STM32 上电时 GPIO 浮空期间可能产生的毛刺
  for (i = 0; i < 16; i++) {
    MAX7219_CLK_H();
    Delay_us(5);
    MAX7219_CLK_L();
    Delay_us(5);
  }
  Delay_ms(5);

  // ====== 两次完整初始化（首次可能因 POR 未完成而失败） ======
  // 第一次：可能被忽略，但让 MAX7219 内部状态机同步
  MAX7219_Write(MAX7219_SHUTDOWN, 0x01);
  Delay_ms(2);
  MAX7219_Write(MAX7219_DISPLAYTEST, 0x00);
  Delay_ms(2);
  MAX7219_Write(MAX7219_SCANLIMIT, 0x07);
  Delay_ms(2);
  MAX7219_Write(MAX7219_DECODEMODE, 0xFF);
  Delay_ms(2);
  MAX7219_Write(MAX7219_INTENSITY, 0x0C);
  Delay_ms(2);

  // ====== 第二次：确保寄存器写入生效 ======
  MAX7219_Write(MAX7219_SHUTDOWN, 0x01);
  Delay_ms(2);
  MAX7219_Write(MAX7219_DISPLAYTEST, 0x00);
  Delay_ms(2);
  MAX7219_Write(MAX7219_SCANLIMIT, 0x07);
  Delay_ms(2);
  MAX7219_Write(MAX7219_DECODEMODE, 0xFF);
  Delay_ms(2);
  MAX7219_Write(MAX7219_INTENSITY, 0x0C);
  Delay_ms(2);

  MAX7219_Clear();
  Delay_ms(2);
}

// 物理走线方向反转：PCB上左1位=MAX7219地址8，左8位=地址1
// 此函数仅用于数据位（1-8），不用于控制寄存器（0x09-0x0F）
uint8_t MAX7219_RevPos(uint8_t pos) {
  return 9 - pos; // 1→8, 2→7, 3→6, 4→5, 5→4, 6→3, 7→2, 8→1
}

void MAX7219_Display(uint8_t digit, uint8_t value, uint8_t dot_v) {
  MAX7219_Write(MAX7219_RevPos(digit), value | (dot_v ? 0x80 : 0x00));
}

void MAX7219_DisplayBlank(uint8_t digit, uint8_t dot_v) {
  MAX7219_Write(MAX7219_RevPos(digit), MAX7219_BLANK | (dot_v ? 0x80 : 0x00));
}

void MAX7219_Clear(void) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    MAX7219_Write(MAX7219_RevPos(i + 1), MAX7219_BLANK);
  }
}

void MAX7219_ClearPos(uint8_t pos) {
  MAX7219_Write(MAX7219_RevPos(pos), MAX7219_BLANK);
}