#ifndef __IR_RECEIVE_H__
#define __IR_RECEIVE_H__

#include "stc89c52.h"

// 红外接收相关变量
extern unsigned char ir_data[4]; // 存储红外接收的数据，共4字节
extern bit ir_flag;              // 红外接收完成标志

// 函数声明
void IR_Init(void);     // 初始化红外接收
void IR_SendData(void); // 通过串口发送红外数据

#endif // __IR_RECEIVE_H__