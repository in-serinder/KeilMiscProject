#include "SN74HC595N.h"

/* =========================================================================
 *  内部: 595-CTL 控制字缓存
 *  所有 SR595_xxx (CTL 相关) 函数都基于这个缓存, 只改其中的目标位,
 *  保证每项独立控制时其它位不受影响。
 * ========================================================================= */
static u8 s_ctl = 0;

/* =========================================================================
 *  内部: 往指定 595 移位寄存器打入一个字节
 *  先传 bit0 (QA), 后传 bit7 (QH); 打完 8 位后拉高 SRCLK 锁存输出。
 *  ser/clk/latch 用宏传入对应的引脚 (如 DQ_CTL_SER, DQ_CTL_CLK, DQ_CTL_SRCLK)
 * ========================================================================= */
#define SR595_SHIFT_BYTE(ser, clk, latch, dat)      \
  do {                                              \
    u8 i;                                           \
    for (i = 0; i < 8; i++) {                       \
      (ser)   = ((dat) >> i) & 0x01;                \
      (clk)   = 0;                                  \
      (clk)   = 1;                                  \
    }                                               \
    (latch) = 0;                                    \
    (latch) = 1;                                    \
  } while (0)

/* 只移位不锁存, 用于级联多片时连续打数据 */
#define SR595_SHIFT_BYTE_NOLATCH(ser, clk, dat)     \
  do {                                              \
    u8 i;                                           \
    for (i = 0; i < 8; i++) {                       \
      (ser)   = ((dat) >> i) & 0x01;                \
      (clk)   = 0;                                  \
      (clk)   = 1;                                  \
    }                                               \
  } while (0)

/* =========================================================================
 *  初始化: 所有 595 引脚复位, 并给 595-CTL 一个安全默认值
 *  默认: RAM OE/CE 都拉高(不使能), 595-DQ 不使能, PL=1(165 移位),
 *        两组 RCLR 都拉高(正常) -> 避免上电误动作。
 * ========================================================================= */
void init_SN74HC595N(void)
{
  /* 595-DQ */
  DQ_CTL_SER   = 0;
  DQ_CTL_CLK   = 0;
  DQ_CTL_SRCLK = 0;

  /* 595-A16 */
  A_CTL_SER   = 0;
  A_CTL_CLK   = 0;
  A_CTL_SRCLK = 0;

  /* 595-CTL */
  IC_CTL_SER   = 0;
  IC_CTL_CLK   = 0;
  IC_CTL_SRCLK = 0;

  /* 默认控制字: 除 RCLR 位外全部为 1, RCLR 位也置 1 (正常) */
  s_ctl = CTL_BIT_RAM_OE | CTL_BIT_RAM_CE | CTL_BIT_DQ_OE
        | CTL_BIT_PL165  | CTL_BIT_A16_RCLR | CTL_BIT_DQ_RCLR;

  SR595_CTL_Write(s_ctl);
}

/* =========================================================================
 *  595-DQ: 输出 1 字节数据 (DQ0-DQ7)
 * ========================================================================= */
void SR595_DQ_Write(u8 dat)
{
  SR595_SHIFT_BYTE(DQ_CTL_SER, DQ_CTL_CLK, DQ_CTL_SRCLK, dat);
}

/* =========================================================================
 *  595-A16: 两片级联输出 16 位地址 A0-A15
 *  级联顺序: 先发的数据最终会落在后级(末片)。
 *  这里先发高 8 位 (A8-A15 所在末片), 再发低 8 位 (A0-A7 所在前片),
 *  发完 16 位后统一锁存。
 * ========================================================================= */
void SR595_A16_Write(u16 dat)
{
  /* 高字节先进移位链 (级联后处于末位) */
  SR595_SHIFT_BYTE_NOLATCH(A_CTL_SER, A_CTL_CLK, (u8)(dat >> 8));
  /* 低字节后进 */
  SR595_SHIFT_BYTE_NOLATCH(A_CTL_SER, A_CTL_CLK, (u8)(dat & 0xFF));

  /* 统一锁存 */
  A_CTL_SRCLK = 0;
  A_CTL_SRCLK = 1;
}

/* =========================================================================
 *  595-CTL: 写入 8 位控制字 (QA-QH)
 * ========================================================================= */
void SR595_CTL_Write(u8 ctl)
{
  s_ctl = ctl;
  SR595_SHIFT_BYTE(IC_CTL_SER, IC_CTL_CLK, IC_CTL_SRCLK, ctl);
}

/* =========================================================================
 *  统一接口
 * ========================================================================= */
void SR595_Write(u8 chip, u16 dat)
{
  switch (chip) {
    case SR_DQ:
      SR595_DQ_Write((u8)dat);
      break;
    case SR_A16:
      SR595_A16_Write(dat);
      break;
    case SR_CTL:
      SR595_CTL_Write((u8)dat);
      break;
    default:
      break;
  }
}

/* =========================================================================
 *  595-A16 地址设置
 * ========================================================================= */
void SR595_A16_SetAddr(u16 addr)
{
  SR595_A16_Write(addr);
}

/* =========================================================================
 *  内部: 修改 595-CTL 中某一位并立即输出
 * ========================================================================= */
static void ctl_apply(void)
{
  SR595_CTL_Write(s_ctl);
}

static void ctl_set_bit(u8 mask, u8 level)
{
  if (level)
    s_ctl |= mask;
  else
    s_ctl &= (u8)~mask;
  ctl_apply();
}

/* =========================================================================
 *  595-CTL 各项独立控制
 *  注意: OE/CE 为低有效, 所以 enable 参数直接映射为电平:
 *        enable=1 -> 输出 0 (使能); enable=0 -> 输出 1 (禁止)
 *  RCLR/PL 等直接按电平控制。
 * ========================================================================= */

/* RAM OE 输出使能 (低有效) */
void SR595_RAM_OE(u8 enable)
{
  ctl_set_bit(CTL_BIT_RAM_OE, enable ? 0 : 1);
}

/* RAM 片选 CE (低有效) */
void SR595_RAM_CE(u8 enable)
{
  ctl_set_bit(CTL_BIT_RAM_CE, enable ? 0 : 1);
}

/* 595-DQ 输出使能 (低有效) */
void SR595_DQ_OE(u8 enable)
{
  ctl_set_bit(CTL_BIT_DQ_OE, enable ? 0 : 1);
}

/* 165 的 PL 电平: high=1 正常(可控移位), high=0 装载(并行读入) */
void SR595_PL165(u8 high)
{
  ctl_set_bit(CTL_BIT_PL165, high ? 1 : 0);
}

/* 595-A16 寄存器清零 (低有效): high=1 正常, high=0 清零 */
void SR595_A16_RCLR(u8 high)
{
  ctl_set_bit(CTL_BIT_A16_RCLR, high ? 1 : 0);
}

/* 595-DQ 寄存器清零 (低有效): high=1 正常, high=0 清零 */
void SR595_DQ_RCLR(u8 high)
{
  ctl_set_bit(CTL_BIT_DQ_RCLR, high ? 1 : 0);
}

/* 读取当前控制字缓存 */
u8 SR595_CTL_Get(void)
{
  return s_ctl;
}
