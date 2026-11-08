#ifndef __EE_H__
#define __EE_H__

/* =====================================================================
 * ee.h — 24Cxx 访问层
 *
 *   IIC1（硬件 I2C）：阵列 8 片 24Cxx，地址 0x50~0x57
 *   IIC2（软件 I2C）：属性存储器 24C02，地址 0x51（256 B，1 字节片内地址）
 *
 * 型号：由 SET 的"型号"字段决定，全部按 2 字节片内地址访问
 *   （24C32 及以上都是 2 字节地址；本阵列不装 24C01~24C16，
 *     若以后要装，把 EE_TypeAddrBytes 的判断补上即可）。
 * ===================================================================== */

#include "board.h"

/* 型号编号（与协议 SET 字段 5 的 Kbit 数值对应）*/
#define EE_MODEL_24C32          32
#define EE_MODEL_24C64          64
#define EE_MODEL_24C128         128
#define EE_MODEL_24C256         256
#define EE_MODEL_24C512         512
#define EE_MODEL_24C1024        1024

typedef struct
{
    uint32_t max_addr;      /* 片内末地址 */
    uint16_t page;          /* 页大小（字节）*/
    uint32_t cap_bytes;     /* 单片刻容量 */
    uint8_t  addr_bytes;    /* 片内地址字节数（1 或 2）*/
} ee_model_t;

/* ---- 型号表 ---- */
const ee_model_t code *EE_Model(uint16_t kbit);

/* ---- 阵列（IIC1）---- */
void     EE_InitArray(void);
void     EE_SetModel(uint16_t kbit);            /* SET 型号字段 */
uint16_t EE_GetModel(void);
uint32_t EE_MaxAddr(void);                      /* 当前型号末地址 */
uint32_t EE_ChipBytes(void);                    /* 单片刻容量 */

bit      EE_WriteBlock(uint8_t chipIdx, uint32_t addr, const uint8_t *buf, uint8_t len);
bit      EE_ReadBlock(uint8_t chipIdx, uint32_t addr, uint8_t *buf, uint8_t len);
bit      EE_Probe(uint8_t chipIdx);
uint8_t  EE_ScanMask(void);                     /* bit0~bit7 = 0x50~0x57 有应答 */

/* ---- 24C02 元数据（IIC2）---- */
void     M24_Init(void);
bit      M24_Read(uint8_t addr, uint8_t *buf, uint8_t len);
bit      M24_Write(uint8_t addr, const uint8_t *buf, uint8_t len);

/* ---- 写周期等待 ---- */
/* 写周期等待：策略由 board.h 的 CFG_EE_ACK_POLL / CFG_EE_WR_WAIT_MS 控制 */

#endif /* __EE_H__ */
