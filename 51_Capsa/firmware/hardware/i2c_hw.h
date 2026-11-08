#ifndef __I2C_HW_H__
#define __I2C_HW_H__

/* =====================================================================
 * i2c_hw.h — IIC1 硬件 I2C 主机（STC8 I2C 外设，P3.2=SCL / P3.3=SDA）
 *
 * 寄存器要点（stc8g1k.h / 手册第 20 章）：
 *   I2CCFG  (0xFE80): bit7=ENI2C bit6=MSSL bits5:0=MSSPEED
 *                     f_SCL = FOSC / 2 / (MSSPEED*2 + 4)
 *   I2CMSCR (0xFE81): bits3:0 = 命令
 *                     1=START  2=SEND  3=RECV_ACK  4=RECV  5=SEND_ACK  6=STOP
 *   I2CMSST (0xFE82): bit6=MSIF(命令完成) bit1=MSACKI(1=NACK) bit0=MSACKO(1=NACK)
 *   P_SW2   (0xBA):   bit7=EAXFR（访问 xdata SFR 前必须置 1）bits5:4=I2C_S
 *
 * ★ I2CCFG 必须写成 0xC0 | MSSPEED：0xE0 会把 MSSPEED 的最高位吃掉，
 *   速度算错（历史踩坑，见 51_24Cxx_Copier/firmware）。
 * ===================================================================== */

#include "board.h"

extern uint8_t I2C1_Err;   /* 0=OK 1=器件地址NACK 2=第1字节NACK 3=第2字节NACK 4=数据NACK 5=读地址NACK */

void    I2C1_Init(void);
void    I2C1_Recover(void);      /* 从机拉住 SDA 时的总线恢复（9 个时钟 + 手工 STOP）*/

/* 运行期切换 I2C 引脚组（P_SW2.I2C_S = sel）：
 * 0 = P1.5/P1.4，1 = P2.5/P2.4，2 = P7.7/P7.6，3 = P3.2/P3.3
 * 用于诊断"到底哪组引脚上挂着阵列"。*/
void    I2C1_SetPinSel(uint8_t sel);

void    I2C1_Start(void);
void    I2C1_Stop(void);
void    I2C1_SendByte(uint8_t b);
uint8_t I2C1_RecvByte(void);
bit     I2C1_WaitAck(void);      /* 1 = NACK */
void    I2C1_SendAck(bit nack);

/* ---- 组合事务（推荐直接用这两个）----
 * I2C1_Write    : START + addr+W + buf... + STOP，返回 1 成功
 * I2C1_WriteRead: START + addr+W + wbuf... + 重复START + addr+R + rbuf... + STOP
 */
bit     I2C1_Write(uint8_t addr7, const uint8_t *buf, uint8_t len);
bit     I2C1_WriteRead(uint8_t addr7, const uint8_t *wbuf, uint8_t wn,
                       uint8_t *rbuf, uint8_t rn);

/* ---- 只探 ACK（不读写数据），用于上电自检 / 排线 ---- */
bit     I2C1_Probe(uint8_t addr7);

/* 空闲时采样 SCL/SDA 电平：bit0=SCL(P3.2) bit1=SDA(P3.3)，1=高。
 * 正常应为 3；若为 0/1/2 说明对应线被拉死或没上拉。*/
uint8_t I2C1_BusLevels(void);

#endif /* __I2C_HW_H__ */
