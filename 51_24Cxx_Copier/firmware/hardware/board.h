#ifndef __BOARD_H__
#define __BOARD_H__

/* =====================================================================
 * board.h  —  板级配置（引脚 / 时钟 / 全局编译开关）
 *
 * MCU : STC8G1K08A (1T 内核，22.1184MHz，SOP8)
 * 换板子只需要改本文件，其它源文件不要出现硬编码引脚。
 * ===================================================================== */

#include "stc8g1k.h"
#include <intrins.h>

/* ---------------- 系统时钟 ---------------- */
#define FOSC_HZ                 22118400UL

/* ---------------- 串口1：P3.0=RXD  P3.1=TXD  115200 8N1 ---------------- */
#define CFG_BAUDRATE            115200

/* ---------------- 硬件 I2C 引脚 ----------------
 * P_SW2.I2C_S[1:0] 的选择表**因芯片而异**，抄错就是"总线不动"：
 *
 *   STC8G1K08A / STC8G1K08-8Pin 系列（本板）：
 *       00 -> SCL=P3.2  SDA=P3.3     <-- 默认值，也是本板接线
 *       01 -> SCL=P5.4  SDA=P5.5     （会占用数码管引脚，别选）
 *       10 -> 无
 *       11 -> 无  （保留值，写 11 等于 I2C 不接任何脚）
 *
 *   其它 STC8G（如 STC8G1K17A）：
 *       00 -> SCL=P1.5  SDA=P1.4
 *       01 -> SCL=P2.5  SDA=P2.4
 *       10 -> SCL=P7.7  SDA=P7.6
 *       11 -> SCL=P3.2  SDA=P3.3
 *
 * ★ 曾经踩过的坑：在 08A 上填 3（=11）是保留值，I2C 不驱动任何引脚 ——
 *   SCL 实测不动、I2CRXD 恒读 0xFF、ACK 又恰好被判成 0 所以不报错。
 */
#define CFG_MCU_IS_08A          1       /* 1 = STC8G1K08A / STC8G1K08-8Pin */

#if CFG_MCU_IS_08A
#define CFG_I2C_PIN_SEL         0       /* 00 -> SCL=P3.2 SDA=P3.3 */
#else
#define CFG_I2C_PIN_SEL         3       /* 11 -> SCL=P3.2 SDA=P3.3 */
#endif

/* 上面两种配置下 I2C 都落在 P3.2/P3.3，总线恢复才有意义 */
#define CFG_I2C_ON_P32_P33      1
#define I2C_SCL                 P32
#define I2C_SDA                 P33

/* ---------------- TM1637 四位数码管：P5.4=CLK  P5.5=DIO ---------------- */
#define TM1637_CLK              P54
#define TM1637_DIO              P55

/* ---------------- EEPROM 写保护 WP（低=允许写） ---------------- */
/* 原 HW_24c64.h 里把 WP 定义成 P5.4/P5.5，与 TM1637 的 CLK/DIO 冲突，
 * 这里挪到 P3.4/P3.5。WP 直接接地时可把 CFG_EEPROM_WP_ENABLE 置 0。 */
#ifndef CFG_EEPROM_WP_ENABLE
#define CFG_EEPROM_WP_ENABLE    1
#endif
#define EE_WP_CH0               P34
#define EE_WP_CH1               P35

/* ---------------- 协议参数 ---------------- */
#define CFG_PKT_BYTES           16      /* 每包字节数，协议固定 */
#define CFG_FRAME_MAX           64      /* 单帧最长内容（W 包 51 字节，留余量） */
#define CFG_RXOK_TIMEOUT_MS     2000    /* 读流程等 >RX-OK< 的超时 */

/* ---------------- 功能开关 ---------------- */
#ifndef CFG_DISPLAY_ENABLE
#define CFG_DISPLAY_ENABLE      1       /* 0 = 不驱动数码管 */
#endif

void Board_Init(void);

/* 检查 TM1637 的 CLK/DIO 是否真的受 CPU 控制：推挽输出 0 后读回，
 * 仍为 0 才算正常。返回 0 基本可以确定：
 *   - P5.4 还是复位脚（STC-ISP 里没勾"复位脚用作 I/O 口"），或
 *   - 引脚被外部强上拉 / 接到了别的网络。
 */
bit Board_TM1637PinsOk(void);

#endif /* __BOARD_H__ */
