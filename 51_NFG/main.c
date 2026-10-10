#include "stc90c58.h"
#include "AS6C622_Serial.h"
#include "SN74HC595N.h"
#include "SN74HC165N.h"
#include "uart.h"
#include "delay.h"

/* =========================================================================
 *  165 实时位响应诊断程序
 *
 *  目的: 不依赖 AS6C622, 直接观察三片 165 读回的真实电平, 便于你用镊子/
 *        导线短接某个引脚到 GND(), 立刻看到对应位在串口上变化。
 *
 *  三片 165 (各自独立或级联均可, 本程序分别读三个 GPIO):
 *    165#1 采集 DQ0-DQ7    QH7 -> P2.1  (DQ_CTL_OUT)
 *    165#2 采集 A0-A7      QH7 -> P1.6  (AR_CTL_SER)
 *    165#3 采集 A8-A12     QH7 -> P1.7  (AL_CTL_OUT)
 *
 *  输出格式 (约每 0.5s 刷新一次):
 *    DQ=P21:11111111  A0_7=P16:11111111  A8_12=P17:11111111
 *
 *  判读:
 *    - 短接某个 165#1 的 Dn 到 GND  -> DQ 那一列对应位应变 0
 *    - 短接 165#1 的 QH(pin9) 到 GND -> DQ 那一列应全部变 0 (00000000)
 *    若短接引脚后某一列毫无变化, 说明 MCU 该引脚与对应 165 的 QH 没接通。
 * ========================================================================= */

/* 直接读一个 165 的 8 位原始数据 (从指定引脚)
 * 165 装载后 QH 先输出 bit7, 每个 CLK 输出下一位, 所以先读的放高位 */
static u8 rd_165_pin(u8 pin)
{
  u8 i;
  u8 dat = 0;

  for (i = 0; i < 8; i++) {
    dat <<= 1;
    dat |= (pin ? 1 : 0);
    /* CLK 脉冲: P2.0 (三片 165 共享) */
    CLK165 = 0;
    CLK165 = 1;
  }
  return dat;
}

/* 打印 8 位二进制 */
static void UART_Bin8(u8 v)
{
  u8 i;
  for (i = 0; i < 8; i++) {
    UART_SendByte((v & 0x80) ? '1' : '0');
    v <<= 1;
  }
}

/* 打印 16 位二进制 */
static void UART_Bin16(u16 v)
{
  UART_Bin8((u8)(v >> 8));
  UART_Bin8((u8)(v & 0xFF));
}

void main(void)
{
  /*测试域*/
  P24 = 1;
init_SN74HC595N();
SR595_CTL_Write(0x00);


while (1) ;

  /*测试域*/


  // u8 dq, a07, a812;
  // u16 cnt = 0;

  // AS6C622_Init();    /* 初始化 595/165 及 RAM 空闲态 */
  // UART_Init();       /* 初始化串口 115200@22.061MHz(12T) */

  // UART_SendString((uint8_t *)"\r\n=== 165 real-time bit monitor ===\r\n");
  // UART_SendString((uint8_t *)"short a pin to GND, watch the column change\r\n");
  // UART_SendString((uint8_t *)"DQ=P21  A0_7=P16  A8_12=P17\r\n\r\n");

  // /* 让 595-DQ 驱动总线为 0x00, 并让 RAM 不选中(不干扰总线),
  //  * 这样 165#1 采集到的应该是 595-DQ 输出的 0x00 (如果链路正常)。
  //  * 你也可以把 595-DQ 输出改成 0xFF 观察。 */
  // SR595_RAM_OE(0);   /* RAM 不驱动 */
  // SR595_RAM_CE(0);   /* RAM 不选中 */
  // SR595_DQ_OE(1);    /* 595-DQ 输出使能, 驱动 DQ 总线 */
  // SR595_DQ_Write(0x00);

  // /* P1.6 / P1.7 / P2.1 设为输入(准双向口读前先写 1) */
  // AR_CTL_SER = 1;
  // AL_CTL_OUT = 1;
  // DQ_CTL_OUT = 1;

  // while (1) {
  //   /* PL 脉冲: 三片同时锁存当前总线电平 (PL 由 595-CTL 的 QD 控制) */
  //   SR595_PL165(0);
  //   SR595_PL165(1);

  //   /* 分别读三片 165 (每片读 8 位, 共享 CLK) */
  //   dq   = rd_165_pin(DQ_CTL_OUT);   /* P2.1 */
  //   a07  = rd_165_pin(AR_CTL_SER);   /* P1.6 */
  //   a812 = rd_165_pin(AL_CTL_OUT);   /* P1.7 */

  //   /* 打印 */
  //   UART_SendString((uint8_t *)"DQ =");
  //   UART_Bin8(dq);
  //   UART_SendString((uint8_t *)"  A07=");
  //   UART_Bin8(a07);
  //   UART_SendString((uint8_t *)"  A8_12=");
  //   UART_Bin8(a812);
  //   UART_SendString((uint8_t *)"\r\n");

  //   cnt++;
  //   delay_ms(500);

  //   /* 每约 5 秒换一次 595-DQ 总线电平, 便于观察总线是否被 165 采到
  //    * (若 '===' 行出现则说明串口活着) */
  //   if ((cnt % 10) == 0) {
  //     UART_SendString((uint8_t *)"--- toggle bus ---\r\n");
  //     SR595_DQ_Write(0xFF);
  //   } else if ((cnt % 10) == 5) {
  //     SR595_DQ_Write(0x00);
  //   }
  // }
}
