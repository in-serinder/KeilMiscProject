#ifndef __I2CM_H__
#define __I2CM_H__

#include "soft_i2c.h"

/* ==========================================================================
 * 软 I2C —— 主机（Master）
 * --------------------------------------------------------------------------
 * 分两级接口：
 *
 *   低层（字节级）：Start / Restart / Stop / WriteByte / ReadByte
 *     用来拼任意事务。比如"先写 3 个字节，重复起始，再读 2 个"：
 *         I2CM_Start();
 *         I2CM_WriteByte(addr << 1);
 *         I2CM_WriteByte(cmd);
 *         I2CM_WriteByte(0x12);
 *         I2CM_Restart();
 *         I2CM_WriteByte((addr << 1) | 1);
 *         a = I2CM_ReadByte(1);
 *         b = I2CM_ReadByte(0);
 *         I2CM_Stop();
 *
 *   高层（帧级）：WriteFrame / ReadFrame / WriteRead / Probe / Scan
 *     覆盖 90% 的场景，不用手写时序。
 *
 * 全部接口不依赖 UART、不依赖任何外设，只用到 soft_i2c.h 里那两根脚。
 * 返回值统一：bit 为 1 表示成功；具体错误原因用 I2CM_LastError() 取。
 * ========================================================================== */

void I2CM_Init(void);                   /* 初始化并做总线恢复，返回时 SDA=1 */
void I2CM_SetSpeed(unsigned char loops);/* 运行时改速率，0=最快，2=约77kHz */
unsigned char I2CM_Speed(void);

/* ---- 低层：字节级时序 ---- */
bit I2CM_Start(void);                   /* 1=成功，0=总线忙/卡死 */
bit I2CM_Restart(void);                 /* 重复起始（读之前用） */
void I2CM_Stop(void);
bit I2CM_WriteByte(unsigned char dat);  /* 1=从机应答(ACK) */
unsigned char I2CM_ReadByte(bit nack);  /* nack=1：读完发 NACK（最后一个字节） */

/* ---- 高层：帧级收发 ---- */
bit I2CM_WriteFrame(unsigned char addr, unsigned char *buf, unsigned char len);
bit I2CM_ReadFrame(unsigned char addr, unsigned char *buf, unsigned char len);

/* 一条事务里"先写后读"（写命令 + 重复起始 + 读数据），最通用的一条。
   wlen=0 时等价于 ReadFrame；rlen=0 时等价于 WriteFrame。 */
bit I2CM_WriteRead(unsigned char addr,
                   unsigned char *wbuf, unsigned char wlen,
                   unsigned char *rbuf, unsigned char rlen);

/* 寄存器模型的便捷封装（和 24Cxx 习惯一致），内部就是 WriteRead。
   默认不编译，需要时把 soft_i2c.h 里的 I2CM_USE_REG_HELPERS 置 1。 */
#if I2CM_USE_REG_HELPERS
bit I2CM_WriteReg(unsigned char addr, unsigned char reg, unsigned char dat);
bit I2CM_ReadReg(unsigned char addr, unsigned char reg, unsigned char *dat);
#endif

/* 探测 / 扫描：多机时先扫一遍，把在线节点列出来 */
bit I2CM_Probe(unsigned char addr7);
unsigned char I2CM_Scan(unsigned char *out, unsigned char max);

/* 上一次操作的错误码（I2C_OK / NACK / BUSY / PARAM） */
unsigned char I2CM_LastError(void);

#endif /* __I2CM_H__ */
