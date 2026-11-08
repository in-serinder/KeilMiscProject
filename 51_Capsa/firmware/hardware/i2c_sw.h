#ifndef __I2C_SW_H__
#define __I2C_SW_H__

/* =====================================================================
 * i2c_sw.h — IIC2 软件 I2C 主机
 *
 * 引脚组**运行时可选**：默认 P5.4=SCL / P5.5=SDA；若上电自检发现这两个脚
 * 推挽拉不低（P5.4 还是复位脚 / 未bonding / 板子其实接在 P1 组），
 * 自动改用 P1.4=SCL / P1.5=SDA。可用 I2C2_PinPair() 查询当前用哪组。
 *
 * 为什么是软件 I2C：STC8G1K17A 只有一个 I2C 外设（P5.4/P5.5 在本片上没有
 * I2C 复用；手册里 01→P5.4/P5.5 那张表只适用于 8 脚 STC8G1K08/08A）。
 *
 * 电气：准双向输出（写 1 = 弱上拉高，写 0 = 强下拉），外部 4.7K 上拉更稳。
 * 不用开漏：开漏的"高"完全靠上拉，一旦内部上拉（P5PU）没生效，
 * 写 1 就是悬空，整条总线永远上不去，表现为"引脚像没被驱动"。
 * 速率：CFG_SW_I2C_DELAY 个空循环半周期，默认约 200kHz。
 * ===================================================================== */

#include "board.h"

extern uint8_t I2C2_Err;

void    I2C2_Init(void);
void    I2C2_Recover(void);

/* 当前引脚组真受 CPU 控制（推挽拉低能读回 0）*/
bit     I2C2_PinsOk(void);

/* 当前使用的引脚组：0 = P5.4/P5.5，1 = P1.4/P1.5（I2C2_Init 自动选择）*/
uint8_t I2C2_PinPair(void);

/* 诊断命令动过引脚后调用：把当前组恢复成准双向并释放总线 */
void    I2C2_RestorePins(void);

void    I2C2_Start(void);
void    I2C2_Stop(void);
bit     I2C2_SendByte(uint8_t b);        /* 返回 1 = 收到 ACK */
uint8_t I2C2_RecvByte(bit nack);
bit     I2C2_Write(uint8_t addr7, const uint8_t *buf, uint8_t len);
bit     I2C2_WriteRead(uint8_t addr7, const uint8_t *wbuf, uint8_t wn,
                       uint8_t *rbuf, uint8_t rn);
bit     I2C2_Probe(uint8_t addr7);

#endif /* __I2C_SW_H__ */
