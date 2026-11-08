#ifndef __BOARD_H__
#define __BOARD_H__

/* =====================================================================
 * board.h — 板级配置（STC8G1K17A / 22.1184MHz）
 *
 * 硬件拓扑（见原理图）：
 *   IIC1  P3.2=SCL  P3.3=SDA  [硬件 I2C, I2C_S=11]
 *         8 × 24Cxx (0x50~0x57)              ← 纯存储器总线，无其它器件
 *   IIC2  P5.4=SCL  P5.5=SDA  [软件 I2C]
 *         24C02   (0x51, 256 B 属性存储)
 *         PCF8574T(0x20, 8 路写保护控制)
 *         SSD1315 (0x3C, 128×32 OLED)
 *         ← 显示 + 写保护控制 + 属性存储，三种功能共用本总线
 *
 * ★ 为什么 IIC2 是软件 I2C：
 *   STC8G1K17A 只有一个 I2C 外设，可用引脚组只有 P1.5/P1.4(I2C_S=00)
 *   与 P3.2/P3.3(I2C_S=11)。手册 2.3.5 管脚说明里 P5.4/P5.5 没有任何
 *   I2C 复用功能（那是 8 脚 STC8G1K08A 的 SCL_2/SDA_2 映射）。
 *   P5.4/P5.5 只能 GPIO 模拟，所以 IIC2 走软件 I2C。
 *   （若日后改板把 IIC2 器件挪到 P1.4/P1.5，那组可以做硬件 I2C，但需要把
 *    i2c_sw.c 换成硬件实现——本板软 I2C 约 200kHz，驱动 24C02/PCF/OLED
 *    已经够用，故未提供切换开关。）
 * ===================================================================== */

#include "stc8g1k.h"
#include <intrins.h>

/* ---------------- 系统时钟 ---------------- */
#define FOSC_HZ                 22118400UL

/* ---------------- 串口1：P3.0=RXD  P3.1=TXD  115200 8N1 ---------------- */
#define CFG_BAUDRATE            115200

/* ---------------- 总线 A：IIC1（硬件 I2C）----------------
 *
 * ★ 实测（本板 1K17A，用 `>SCAN<` 扫过四种取值）：
 *     I2C_S = 0 或 2  ->  SCL=P3.2  SDA=P3.3   （阵列 8 片全部应答，位图 FF）
 *     I2C_S = 1 或 3  ->  SCL=P1.5  SDA=P1.4   （看到的是 IIC2 上的 OLED/24C02）
 *   即本片只解码 I2C_S 的最低位（P_SW2.bit4），bit5 不起作用，
 *   而且 00 指向的是 P3.2/P3.3 —— 与手册"通用 STC8G：00→P1.5/P1.4"那张表
 *   不一致（手册那两张表对本封装都不准，以实测为准）。
 *   若哪天换片子，先跑 `>SCAN<` 确认阵列在哪一组，再改这个宏。
 *
 * f_SCL = FOSC / 2 / (MSSPEED*2 + 4)
 * ---------------------------------------------------------------------- */
#define CFG_I2C_S_SEL           0       /* 00 -> P3.2/P3.3（实测，阵列所在）*/

/* ---------------- 阵列总线时钟（按目标频率自动换算 MSSPEED）----------------
 *
 * CFG_I2C_SPEED_KHZ：目标时钟（kHz），三档：
 *   250   -> 251.3 kHz   任何 24Cxx / 任何走线质量都稳定
 *   400   -> 395.4 kHz   24C32+ 全系规格上限，默认档
 *   1000  -> 921.6 kHz   ★仅 4.5~5.5V 供电的 24Cxx 支持（5V 版 1MHz 规格）；
 *                          要求走线短、上拉强（1~2.2k）。开档后先跑
 *                          初始化（芯片测试会整片读写校验）确认稳定再用。
 * 其它值 -> #error，防手滑写出超规格的频率。
 * 换算：MSSPEED = ceil(FOSC / (2*(2*F+4kHz)))，三档分别对应 20 / 12 / 4。
 * ---------------------------------------------------------------------- */
#define CFG_I2C_SPEED_KHZ       400

#if CFG_I2C_SPEED_KHZ <= 250
#define CFG_I2C_HW_MSSPEED      20      /* 251.3 kHz */
#elif CFG_I2C_SPEED_KHZ <= 400
#define CFG_I2C_HW_MSSPEED      12      /* 395.4 kHz */
#elif CFG_I2C_SPEED_KHZ <= 1000
#define CFG_I2C_HW_MSSPEED      4       /* 921.6 kHz */
#else
#error "CFG_I2C_SPEED_KHZ: unsupported frequency"
#endif

/* ---------------- 阵列写周期等待策略 ----------------
 *
 * CFG_EE_ACK_POLL：
 *   0 = 固定延时：写完干等 CFG_EE_WR_WAIT_MS（最保守，任何 24Cxx 都稳）
 *   1 = ACK polling（默认）：写完后持续发 START+器件地址，芯片回 ACK 即写
 *       周期结束。tWR 典型值 1~2 ms（5 ms 只是规格上限），平均每页省 3~4 ms，
 *       整卡写入时间大约省一半。保底超时 CFG_EE_ACK_POLL_TMO_MS，超时后
 *       退回固定延时，防廉价芯片在写周期内错误 ACK。
 * 5V 24Cxx 建议保持 1；怀疑芯片兼容性（写后读回数据不对）时改 0 排除。
 * ---------------------------------------------------------------------- */
#define CFG_EE_ACK_POLL         1
#define CFG_EE_WR_WAIT_MS       5       /* 固定延时模式 / polling 超时兜底用 */
#define CFG_EE_ACK_POLL_TMO_MS  10

/* ---------------- 总线 B：IIC2（软件 I2C，P5.4/P5.5）---------------- */
#define SW_SCL                  P54
#define SW_SDA                  P55
#define SW_SCL_PU               P5PU             /* 内部 4K 上拉，弱驱动时可用 */
#ifndef CFG_SW_I2C_DELAY
#define CFG_SW_I2C_DELAY        6               /* 半周期空循环单元数（约 200kHz） */
#endif

/* ---------------- 设备地址（7 位）---------------- */
#define ADDR_ARRAY_BASE         0x50    /* 阵列 8 片：0x50 ~ 0x57（IIC1）*/
#define ADDR_META_24C02         0x51    /* 属性存储器（IIC2）*/
#define ADDR_PCF8574            0x20    /* 8 路写保护（IIC2）*/
#define ADDR_OLED               0x3C    /* SSD1315（IIC2）*/

/* ---------------- 存储卡参数 ---------------- */
#define CFG_CHIP_COUNT          8       /* 阵列片数 */
#define CFG_CARD_CAP_BYTES      8192    /* 单片刻容量（默认 24C64 = 8KB）*/

/* ---------------- 协议参数 ---------------- */
#define CFG_PKT_BYTES           16      /* 每包固定字节数 */
#define CFG_FRAME_MAX           72      /* 单帧最长内容（W 包 = 3 + 47 = 50，留余量） */
#define CFG_RXOK_TIMEOUT_MS     2000    /* 读流程等 [RX-OK] 的超时 */

/* ---------------- 显示开关 ---------------- */
#ifndef CFG_DISPLAY_ENABLE
#define CFG_DISPLAY_ENABLE      1
#endif

void Board_Init(void);

/* IIC2 的 SCL/SDA 必须是真实可控的 GPIO（P5.4 若被 ISP 配成复位脚会失效）*/
bit Board_SwI2cPinsOk(void);

#endif /* __BOARD_H__ */
