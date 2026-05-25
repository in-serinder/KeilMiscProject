#include "stc89c52.h"

#ifndef __PCF8574_H__
#define __PCF8574_H__

// PCF8574基础地址 (A2=A1=A0=0)
#define PCF8574_ADDR 0x20
#define PCF8574_W_ADDR (PCF8574_ADDR << 1)        // 写地址: 0x40
#define PCF8574_R_ADDR (PCF8574_ADDR << 1 | 0x01) // 读地址: 0x41

// PCF8574端口定义
#define PCF8574_P0 0
#define PCF8574_P1 1
#define PCF8574_P2 2
#define PCF8574_P3 3
#define PCF8574_P4 4
#define PCF8574_P5 5
#define PCF8574_P6 6
#define PCF8574_P7 7

// 端口状态定义
#define PCF8574_LOW 0
#define PCF8574_HIGH 1

#define SCL P20
#define SDA P21

// 初始化PCF8574
void PCF8574_Init(void);

// 读取PCF8574所有端口状态
unsigned char PCF8574_Read(void);

// 写入PCF8574所有端口状态
void PCF8574_Write(unsigned char dat);

// 设置指定端口状态 (port: 0-7, state: 0=低电平, 1=高电平)
void PCF8574_SetPort(unsigned char port, bit state);

// 读取指定端口状态
bit PCF8574_GetPort(unsigned char port);

#endif