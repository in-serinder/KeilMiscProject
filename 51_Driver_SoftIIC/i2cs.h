#ifndef __I2CS_H__
#define __I2CS_H__

#include "soft_i2c.h"

/* ==========================================================================
 * 软 I2C —— 从机（Slave）
 * --------------------------------------------------------------------------
 * 8051 没有 I2C 外设，从机只能靠**高频轮询**：主循环里不停调 I2CS_Poll()，
 * 每次读一下 SCL/SDA 当前电平，和上一次比较，只在边沿上推进状态机。
 * 所以它不是阻塞函数，可以在主循环里和各路任务共存。
 *
 * 两种工作方式，按需要选：
 *
 *   1) 回调式（推荐，通用）
 *        I2CS_SetHandlers(on_write, on_read);
 *      主机写来一帧时，收完（STOP 或重复起始）调 on_write(buf, len)；
 *      主机要读时，调 on_read(buf, max) 让用户填数据并返回字节数。
 *      帧内容、命令解析、长度全由用户决定，库不预设任何结构。
 *
 *   2) 寄存器文件模式（不装回调时的默认行为）
 *      主机发 "reg + data..."，从机把 data 存进内置 8 字节寄存器文件；
 *      主机读时从寄存器指针开始自动递增返回。习惯同 24Cxx。
 *      用 I2CS_GetReg / I2CS_SetReg 在本地读写。
 *
 * 多节点：每片用 I2CS_SetAddr(0x50 + 节点号) 在初始化时设自己的地址，
 *        同一份固件可以烧到多片板子上，只有地址不同。
 * ========================================================================== */

#ifndef I2CS_BUF_SIZE
#define I2CS_BUF_SIZE       16      /* 单帧最大字节数（收发共用一个缓冲区） */
#endif

#if I2CS_USE_REG_FILE
#ifndef I2CS_REG_COUNT
#define I2CS_REG_COUNT      8       /* 寄存器文件大小，仅寄存器模式使用 */
#endif
#endif

#ifndef I2CS_DEFAULT_ADDR
#define I2CS_DEFAULT_ADDR   0x50    /* 7 位地址 */
#endif

/* 1 = 也应答广播地址 0x00（所有节点同时收，只能写不能读） */
#ifndef I2CS_ACCEPT_BCAST
#define I2CS_ACCEPT_BCAST   1
#endif

/* ---- 回调原型 ---- */
typedef void (*I2CS_OnWrite)(unsigned char *buf, unsigned char len);
typedef unsigned char (*I2CS_OnRead)(unsigned char *buf, unsigned char max);

void I2CS_Init(void);
void I2CS_Poll(void);                       /* 主循环里尽可能频繁地调 */

void I2CS_SetAddr(unsigned char addr7);     /* 运行时改自己的地址 */
unsigned char I2CS_Addr(void);
void I2CS_SetHandlers(I2CS_OnWrite on_write, I2CS_OnRead on_read);

bit I2CS_BusIdle(void);                     /* 1 = 当前没有事务（可以干慢活） */
unsigned char I2CS_State(void);             /* 调试用：当前状态号 */

#if I2CS_USE_REG_FILE
/* 不装回调时的"简易共享内存"。主机发 [reg][data...]，这里就能读回。 */
unsigned char I2CS_GetReg(unsigned char i);
void I2CS_SetReg(unsigned char i, unsigned char v);
#endif

#endif /* __I2CS_H__ */
