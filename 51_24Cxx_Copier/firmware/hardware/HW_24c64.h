#ifndef __HW_24C64_H__
#define __HW_24C64_H__

/* =====================================================================
 * HW_24C64.h — 24Cxx 系列（24C01~24C1024）读写驱动 @ STC8G 硬件 I2C
 *
 * 重要：这里把"槽位地址"和"芯片型号"彻底解耦了。
 *   槽位地址（0x50~0x57）只决定总线上的从机地址；
 *   芯片型号（决定片内地址字节数、页大小、容量上限）由 EE_SLOT_TYPE 配置。
 *   默认 8 个槽位都是 24C64 —— 板子上插什么就配什么，见 HW_24c64.c。
 *
 * 特性：
 *   - 片内地址 1 字节 / 2 字节自适应（24C01~24C16 为 1 字节）
 *   - 24C04/08/16 的高位地址（P0/P1/P2）自动并入器件地址
 *   - 24C1024 的第 17 位地址（A16）按掩码并入器件地址
 *   - 页写自动按页边界拆分（跨页会回卷，必须拆）
 *   - 顺序读（一包 16 字节只需一次 START）
 *   - 写周期等待以宏配置：默认固定 5ms，也可改为 ACK 轮询
 * ===================================================================== */

#include "board.h"

/* ---------- I2C 总线速率 ----------
 * STC8 手册：f_SCL = FOSC / 2 / (MSSPEED*2 + 4)
 * 22.1184MHz 下的常用取值：
 *   MSSPEED = 53 -> 100.5kHz（兼容老 24C01/24C02 的 100kHz 上限）
 *   MSSPEED = 20 -> 251.3kHz（默认，和模拟器一致）
 *   MSSPEED = 12 -> 395.0kHz（仍在 24Cxx 的 400kHz 上限内）
 * MSSPEED 只有 6 位（0~63），本主频可调范围约 85kHz ~ 1.4MHz。
 */
#ifndef EE_I2C_MSSPEED
#define EE_I2C_MSSPEED          20
#endif

/* ---------- 写周期等待（关键可调项） ----------
 * tWR：24C01/02 最大 10ms，24C04~24C1024 最大 5ms。
 * 默认按 24C64 的 5ms 给，实测偏保守最安全；要更快可打开 ACK 轮询。
 */
#ifndef EE_WRITE_CYCLE_MS
#define EE_WRITE_CYCLE_MS       5
#endif

/* 0 = 固定延时 EE_WRITE_CYCLE_MS；1 = ACK 轮询（写完立刻试地址，收到 ACK 即返回） */
#ifndef EE_WC_USE_ACK_POLL
#define EE_WC_USE_ACK_POLL      0
#endif

/* ACK 轮询最大次数（一次约 30us，400 次 ≈ 12ms 兜底） */
#ifndef EE_WC_POLL_MAX
#define EE_WC_POLL_MAX          400
#endif

/* 24C1024 的 A16 在器件地址中的位置：A1 -> 0x02，A0 -> 0x01，板级固定则填 0x00 */
#ifndef EE_1024_A16_MASK
#define EE_1024_A16_MASK        0x02
#endif

/* ---------- 型号编号 ---------- */
#define EE_CHIP_24C01           0
#define EE_CHIP_24C02           1
#define EE_CHIP_24C04           2
#define EE_CHIP_24C08           3
#define EE_CHIP_24C16           4
#define EE_CHIP_24C32           5
#define EE_CHIP_24C64           6
#define EE_CHIP_24C128          7
#define EE_CHIP_24C256          8
#define EE_CHIP_24C512          9
#define EE_CHIP_24C1024         10
#define EE_CHIP_COUNT           11

/* 8 个槽位默认插什么片子。
 *
 * 默认取 24C1024：它是**片内地址 2 字节**系列里容量最大的（max 0x1FFFF），
 * 所以 24C32 / 64 / 128 / 256 / 512 / 1024 插在任意槽位都能直接跑 ——
 *   地址宽度一样（2 字节），EDGE 上限 0x1FFFF 不会误拦大容量型号，
 *   而本协议每次最多写 16 字节且地址 16 对齐，页大小差异不会跨页，无需关心。
 *
 * 只有 **24C01 / 02 / 04 / 08 / 16**（片内地址 1 字节）必须单独配：
 * 把 HW_24c64.c 里 EE_SLOT_TYPE[8] 对应槽位改成自己的型号，否则会多送一个地址字节，
 * 片子收到的不是地址而是数据，读写都会错。
 *
 * 混装板子也是改 EE_SLOT_TYPE[8]，不要改这里。
 */
#ifndef EE_SLOT_CHIP_DEFAULT
#define EE_SLOT_CHIP_DEFAULT    EE_CHIP_24C1024
#endif

/* 单片 24Cxx 的静态描述 */
typedef struct
{
    uint32_t max_addr;    /* 最大字节地址 */
    uint8_t  addr_bytes;  /* 片内地址字节数：1 或 2 */
    uint16_t page;        /* 页写大小（字节）。必须 16 位：24C1024 是 256，uint8_t 会截断成 0 */
    uint8_t  page_bits;   /* 1 字节地址器件：高位地址占用器件地址的位数 */
    uint8_t  hi_mask;     /* 2 字节地址器件：A16 在器件地址中的掩码 */
} ee_dev_t;

extern uint8_t I2C_Err;   /* 0=OK 1=器件地址NACK 2=地址高NACK 3=地址低NACK 4=数据NACK 5=读地址NACK */

/* ---- I2C 基本动作 ---- */
void    I2C_Init(void);
void    I2C_Start(void);
void    I2C_Stop(void);
void    I2C_SendByte(uint8_t byte);
uint8_t I2C_ReceiveByte(void);
bit     I2C_WaitAck(void);
void    I2C_SendAck(bit nack);

/* ---- 槽位选择 ---- */
uint8_t  EE_Select(uint8_t chip7);    /* 1=成功（0x50~0x57） */
uint32_t EE_MaxAddr(void);            /* 当前槽位芯片的最大地址 */

/* ---- 读写（addr 从 0 开始，len 不要跨 EDGE） ---- */
bit     EE_WriteBlock(uint32_t addr, const uint8_t *buf, uint8_t len);
bit     EE_ReadBlock(uint32_t addr, uint8_t *buf, uint8_t len);

/* ---- 调试：只发"器件地址 + 等 ACK"，不读写数据 ----
 * 返回 1 表示该槽位有器件应答（接线/上拉/地址正常），0 表示 NACK 或未选槽位。 */
bit     EE_Probe(void);

/* ---- 调试：依次探测 0x50~0x57 八个槽位，返回位掩码 ----
 *   bit0 = 0x50, bit1 = 0x51, ... bit7 = 0x57；1 = 该地址有器件应答
 * 扫描不改动当前已选的槽位。 */
uint8_t EE_ScanMask(void);

#endif /* __HW_24C64_H__ */
