#ifndef __I2C_DEMO_H__
#define __I2C_DEMO_H__

#include "soft_i2c.h"

/* ==========================================================================
 * 示例层：两片（或多片）8051 之间用软 I2C 互传数据
 * --------------------------------------------------------------------------
 * 这一层才是"应用"，它定义了一套极简命令协议，用来演示库怎么用。
 * 协议内容完全由你自己定，库（i2cm / i2cs）对帧格式一无所知。
 *
 *   帧格式： [0]=CMD  [1]=LEN  [2..]=DATA
 *   CMD: 0x01 写数据   0x02 读数据   0x03 心跳
 *
 * 想完全不用串口（纯 I2C 通信）：
 *   把 soft_i2c.h 里的 I2C_DEMO_LOG 置 0，本文件就再也不 include uart.h。
 * ========================================================================== */

#define I2C_DEMO_ADDR       0x50    /* 本节点地址，多节点时改成 0x50+n */
#define I2C_DEMO_DATA_MAX   8       /* 一帧携带的最大数据字节数 */

#define I2C_CMD_WRITE       0x01
#define I2C_CMD_READ        0x02
#define I2C_CMD_PING        0x03

/* 这块板子跑哪一侧的示例：1 = 主机（扫描 + 收发验证），0 = 从机（只应答）。
   两块板子对烧时，一块 1 一块 0；单板自测（I2C_SELFTEST=1）保持 1。 */
#ifndef I2C_DEMO_AS_MASTER
#define I2C_DEMO_AS_MASTER  1
#endif

/* 主机速率（m_delay 轮数）。9600 之外别无依赖，纯 I2C 链路也是它。
   对端是软件从机时必须 >= 15，否则从机轮询来不及采样。 */
#ifndef I2C_DEMO_SPEED
#define I2C_DEMO_SPEED      20
#endif

void I2C_DemoSlaveInit(void);   /* 从机：设地址 + 装回调，必须在主循环之前 */
void I2C_DemoSlave(void);       /* 从机：轮询应答，不返回 */
void I2C_DemoMaster(void);      /* 主机：扫描 + 收发验证 + 循环，不返回 */

#endif /* __I2C_DEMO_H__ */
