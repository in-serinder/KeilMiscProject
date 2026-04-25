#ifndef __IR_RECEIVE_H__
#define __IR_RECEIVE_H__

#include "stc15w.h"

// 红外接收相关变量
extern unsigned char ir_raw_data[4]; // 存储红外接收的原始数据，共4字节
extern bit ir_receive_complete;       // 红外接收完成标志
extern unsigned char ir_key_code;      // 解码后的按键编码

// 函数声明
void IR_Init(void);             // 初始化红外接收
void IR_SendData(void);         // 通过串口发送红外数据
unsigned char IR_GetKeyCode(void);   // 获取解码后的按键编码
unsigned char IR_GetDigit(void);     // 获取数字按键值（0-9）

#endif // __IR_RECEIVE_H__