#include "stc90c58.h"
#include "AS6C622_Serial.h"
#include "SN74HC595N.h"
#include "SN74HC165N.h"
#include "uart.h"
#include "delay.h"

/* -------------------------------------------------------------------------
 *  串口辅助输出
 * ------------------------------------------------------------------------- */
static void UART_SendUInt(u16 val)
{
  u8 buf[6];
  u8 n = 0;

  if (val == 0) {
    UART_SendByte('0');
    return;
  }

  while (val > 0) {
    buf[n++] = (u8)('0' + (val % 10));
    val /= 10;
  }
  while (n > 0) {
    UART_SendByte(buf[--n]);
  }
}

/* 发送 16 位十六进制数 (高字节在前) */
static void UART_SendHex16(u16 val)
{
  UART_SendHex((u8)(val >> 8));
  UART_SendHex((u8)(val & 0xFF));
}

/* 发送字符串并换行 */
static void UART_PrintLine(uint8_t *str)
{
  UART_SendString(str);
  UART_SendByte('\r');
  UART_SendByte('\n');
}

/* 打印一个字节的 8 位二进制 (方便观察 CTL 各位) */
static void UART_PrintBin8(u8 val)
{
  u8 i;
  for (i = 0; i < 8; i++) {
    UART_SendByte((val & 0x80) ? '1' : '0');
    val <<= 1;
  }
}

/* -------------------------------------------------------------------------
 *  主函数: 逐片测试 595 的控制灵活性 与 165 的读取灵活性, 结果输出到串口
 * ------------------------------------------------------------------------- */
void main(void)
{
  u8  i;
  u8  dq_wr, dq_rd;
  u16 a_wr, a_rd;
  u8  ctl;

  AS6C622_Init();   /* 初始化 595/165 及 RAM 空闲态 */
  UART_Init();      /* 初始化串口 115200@22.1184MHz */

  UART_PrintLine((uint8_t *)"=== 595 / 165 flexibility test ===");

  /* =====================================================================
   *  1. 595-CTL 逐位控制灵活性测试
   *     依次翻转每个控制位, 打印 CTL 缓存, 验证只有目标位改变
   * ===================================================================== */
  UART_PrintLine((uint8_t *)"--- [1] 595-CTL bit control ---");
  ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"init  ctl=0x"); UART_SendHex(ctl);
  UART_SendString((uint8_t *)" b"); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");

  /* RAM OE (低有效): 1=使能(RAM驱动总线) 0=禁止 */
  SR595_RAM_OE(1); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"RAM_OE(1)  ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");
  SR595_RAM_OE(0); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"RAM_OE(0)  ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");

  /* RAM CE (低有效): 1=选中 0=未选中 */
  SR595_RAM_CE(1); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"RAM_CE(1)  ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");
  SR595_RAM_CE(0); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"RAM_CE(0)  ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");

  /* 595-DQ OE (低有效): 1=595-DQ驱动总线 0=高阻 */
  SR595_DQ_OE(1); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"DQ_OE(1)   ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");
  SR595_DQ_OE(0); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"DQ_OE(0)   ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");

  /* 165 PL: 1=移位模式 0=装载 */
  SR595_PL165(0); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"PL165(0)   ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");
  SR595_PL165(1); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"PL165(1)   ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");

  /* 595-A16 RCLR (低有效): 1=正常 0=清零 */
  SR595_A16_RCLR(0); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"A16_RCLR(0) ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");
  SR595_A16_RCLR(1); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"A16_RCLR(1) ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");

  /* 595-DQ RCLR (低有效): 1=正常 0=清零 */
  SR595_DQ_RCLR(0); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"DQ_RCLR(0) ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");
  SR595_DQ_RCLR(1); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"DQ_RCLR(1) ctl=0x"); UART_SendHex(ctl); UART_SendByte(' '); UART_PrintBin8(ctl); UART_PrintLine((uint8_t *)"");

  /* 整体写控制字 (不经过逐位接口) */
  SR595_CTL_Write(0xAA); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"CTL_Write(0xAA) ctl=0x"); UART_SendHex(ctl); UART_PrintLine((uint8_t *)"");
  SR595_CTL_Write(0x55); ctl = SR595_CTL_Get();
  UART_SendString((uint8_t *)"CTL_Write(0x55) ctl=0x"); UART_SendHex(ctl); UART_PrintLine((uint8_t *)"");

  /* 恢复安全态: RAM OE/CE 禁止, 595-DQ 高阻, PL=1, RCLR=1 */
  SR595_RAM_OE(0);
  SR595_RAM_CE(0);
  SR595_DQ_OE(0);
  SR595_PL165(1);
  SR595_A16_RCLR(1);
  SR595_DQ_RCLR(1);

  /* =====================================================================
   *  2. 595-DQ 写 + 165-DQ 读 回环测试
   *     595-DQ 驱动 DQ 总线, 165-DQ 采集同一总线 -> 同时验证两端灵活性
   *     前提: RAM OE/CE 禁止 (不让 RAM 驱动总线)
   * ===================================================================== */
  UART_PrintLine((uint8_t *)"--- [2] 595-DQ write / 165-DQ read loopback ---");
  SR595_DQ_OE(1);   /* 使能 595-DQ 输出驱动总线 */
  {
    u8 patterns[] = {0x00, 0xFF, 0x55, 0xAA, 0x0F, 0xF0, 0x3C, 0xC3};
    for (i = 0; i < 8; i++) {
      dq_wr = patterns[i];
      SR595_DQ_Write(dq_wr);
      dq_rd = SR165_DQ_Read();
      UART_SendString((uint8_t *)"DQ wr=0x"); UART_SendHex(dq_wr);
      UART_SendString((uint8_t *)" rd=0x");   UART_SendHex(dq_rd);
      UART_SendString((uint8_t *)(dq_wr == dq_rd ? "  OK\r\n" : "  MISMATCH\r\n"));
    }
  }
  SR595_DQ_OE(0);   /* 释放总线 */

  /* =====================================================================
   *  3. 595-A16 写 + 165-A12 读 回环测试
   *     595-A16 驱动地址线, 165-A12 采集低 13 位 (A0-A12)
   * ===================================================================== */
  UART_PrintLine((uint8_t *)"--- [3] 595-A16 write / 165-A12 read loopback ---");
  {
    u16 patterns[] = {0x0000, 0x1FFF, 0x1555, 0x0AAA,
                      0x0F0F, 0x00FF, 0x1F00, 0x1234};
    for (i = 0; i < 8; i++) {
      a_wr = patterns[i] & 0x1FFF;        /* 只保留 A0-A12 共 13 位 */
      SR595_A16_Write(a_wr);
      a_rd = SR165_A12_Read() & 0x1FFF;   /* 屏蔽 165 高 3 位浮空 */
      UART_SendString((uint8_t *)"A16 wr=0x"); UART_SendHex16(a_wr);
      UART_SendString((uint8_t *)" rd=0x");    UART_SendHex16(a_rd);
      UART_SendString((uint8_t *)(a_wr == a_rd ? "  OK\r\n" : "  MISMATCH\r\n"));
    }
  }

  /* =====================================================================
   *  4. 统一接口 SR595_Write / SR165_Read 测试
   * ===================================================================== */
  UART_PrintLine((uint8_t *)"--- [4] unified interface SR595_Write / SR165_Read ---");
  SR595_DQ_OE(1);
  SR595_Write(SR_DQ, 0xA5);
  dq_rd = (u8)SR165_Read(SR165_DQ);
  UART_SendString((uint8_t *)"SR_DQ  0xA5 -> rd=0x"); UART_SendHex(dq_rd);
  UART_SendString((uint8_t *)(dq_rd == 0xA5 ? "  OK\r\n" : "  MISMATCH\r\n"));
  SR595_DQ_OE(0);

  SR595_Write(SR_A16, 0x1555);
  a_rd = SR165_Read(SR165_A12) & 0x1FFF;
  UART_SendString((uint8_t *)"SR_A16 0x1555 -> rd=0x"); UART_SendHex16(a_rd);
  UART_SendString((uint8_t *)(a_rd == 0x1555 ? "  OK\r\n" : "  MISMATCH\r\n"));

  /* =====================================================================
   *  5. 165 低层原语灵活性: 手动 SR165_Load + SR165_ShiftReadBit 逐位读
   * ===================================================================== */
  UART_PrintLine((uint8_t *)"--- [5] 165 low-level primitives (manual load + shift) ---");
  SR595_DQ_OE(1);
  SR595_DQ_Write(0x5A);
  {
    u8 manual = 0;
    SR165_Load();
    for (i = 0; i < 8; i++) {
      manual <<= 1;
      manual |= SR165_ShiftReadBit();
    }
    UART_SendString((uint8_t *)"manual DQ rd=0x"); UART_SendHex(manual);
    UART_SendString((uint8_t *)(manual == 0x5A ? "  OK\r\n" : "  MISMATCH\r\n"));
  }
  SR595_DQ_OE(0);

  /* 恢复安全空闲态 */
  AS6C622_Idle();
  UART_PrintLine((uint8_t *)"=== test done, idle ===");

  while (1) {
    delay_ms(1000);
  }
}