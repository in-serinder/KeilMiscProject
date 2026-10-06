#ifndef __SN74HC595N_H__
#define __SN74HC595N_H__

#include "stc90c58.h"

/* 基础类型 (自包含定义, 不改动 stc90c58.h) */
#ifndef TYPE_U8_U16_DEFINED
#define TYPE_U8_U16_DEFINED
typedef unsigned char  u8;
typedef unsigned int   u16;
#endif

// 多篇SN74HC595N芯片

// 控制AS6C622的DQ0-DQ7(映射595N的QA-DQH) 的595控制引脚 这里命名芯片为595-DQ
#define DQ_CTL_SER P13
#define DQ_CTL_CLK P14
#define DQ_CTL_SRCLK P15

// 控制AS6C622的A0-A7以及一片级联595N芯片的A8-A14(映射595N的QA-DQH) 的595控制引脚 这里命名芯片为595-A16
#define A_CTL_SER P10
#define A_CTL_CLK P11
#define A_CTL_SRCLK P12

//杂项控制用595 映射为 QA-AS6C22的OE ; QB-AS6C22的CE ; QC-控制595-DQ的OE ; QD-控制与SN74HC165N芯片相关控制与AS6C622的DQ0-DQ7的PL；QE-控制595-A16芯片的RCLR引脚 ； QF-控制595-DQ的RCLR引脚 这个芯片命名为595-CTL
#define IC_CTL_SER P22
#define IC_CTL_CLK P23
#define IC_CTL_SRCLK P24
//其中QD控制的PL是三片SN74HC165N芯片的PL公用的

/* =========================================================================
 *  595-CTL (杂项控制) 移位寄存器输出位定义
 *  QA=bit0 ... QH=bit7 (共 8 位)
 * ========================================================================= */
#define CTL_BIT_RAM_OE   0x01  /* QA : AS6C622 的 OE       (0=有效) */
#define CTL_BIT_RAM_CE   0x02  /* QB : AS6C622 的 CE       (0=有效) */
#define CTL_BIT_DQ_OE    0x04  /* QC : 595-DQ 的输出使能 OE (0=使能) */
#define CTL_BIT_PL165    0x08  /* QD : 165 的 PL(共用) + AS6C622 DQ  (低=锁存/装载) */
#define CTL_BIT_A16_RCLR 0x10  /* QE : 595-A16 的 RCLR (低=清零) */
#define CTL_BIT_DQ_RCLR  0x20  /* QF : 595-DQ  的 RCLR (低=清零) */

/* 595-DQ / 595-A16 / 595-CTL 芯片编号, 用于统一接口 */
#define SR_DQ    0
#define SR_A16   1
#define SR_CTL   2

/* -------------------------------------------------------------------------
 *  初始化
 * ------------------------------------------------------------------------- */
void init_SN74HC595N(void);

/* -------------------------------------------------------------------------
 *  基础移位操作
 * ------------------------------------------------------------------------- */

/* 往 595-DQ 移入 1 字节 (bit0 先移入 -> QA), 随后锁存输出 */
void SR595_DQ_Write(u8 dat);

/* 往 595-A16 (两片级联, 16 位) 移入数据, 先移高字节(级联末片)再移低字节 */
void SR595_A16_Write(u16 dat);

/* 往 595-CTL 移入 1 字节控制字 (用 CTL_BIT_xxx 组合) */
void SR595_CTL_Write(u8 ctl);

/* 统一接口: chip 取 SR_DQ/SR_A16/SR_CTL, dat 为要输出的值 */
void SR595_Write(u8 chip, u16 dat);

/* -------------------------------------------------------------------------
 *  595-A16 地址线控制 (A0-A15)
 * ------------------------------------------------------------------------- */
void SR595_A16_SetAddr(u16 addr);   /* 直接设置 16 位地址 */

/* -------------------------------------------------------------------------
 *  595-CTL 杂项控制 (每一项独立控制, 立即生效)
 *  enable 参数: 传 1 表示"使能/正常", 0 表示"禁止/清零/锁存"
 *  函数内部会自动保存其他位, 只修改目标位
 * ------------------------------------------------------------------------- */
void SR595_RAM_OE(u8 enable);       /* RAM OE 输出使能 */
void SR595_RAM_CE(u8 enable);       /* RAM 片选 */
void SR595_DQ_OE(u8 enable);        /* 595-DQ 输出使能 */
void SR595_PL165(u8 high);          /* 165 的 PL 电平 (1=正常/移位, 0=装载) */
void SR595_A16_RCLR(u8 high);       /* 595-A16 寄存器清零 (1=正常, 0=清零) */
void SR595_DQ_RCLR(u8 high);        /* 595-DQ  寄存器清零 (1=正常, 0=清零) */

/* 读取当前 595-CTL 控制字缓存 */
u8   SR595_CTL_Get(void);

#endif
// __SN74HC595N_H__
