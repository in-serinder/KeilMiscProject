#ifndef __24C64_H__
#define __24C64_H__

#include "stc89c52.h" //STC89C52的头文件，包含了寄存器定义和函数声明 用的是扁平封装 和Keil自带的reg52.h不同

// 引脚定义
#define SDA P34
#define SCL P35

// 24C64相关定义
#define EEPROM_ADDR 0xA0 // 24C64设备地址

// 函数声明
void I2C_Init(void);
void I2C_Start(void);
void I2C_Stop(void);
void I2C_SendByte(unsigned char byte);
unsigned char I2C_ReceiveByte(void);
bit I2C_WaitAck(void);
void I2C_SendAck(bit ack);

// 24C64操作函数
void EEPROM_WriteByte(unsigned int addr, unsigned char dat);
unsigned char EEPROM_ReadByte(unsigned int addr);
void EEPROM_WriteString(unsigned int addr, unsigned char *str);
void EEPROM_ReadString(unsigned int addr, unsigned char *str, unsigned int len);
void EEPROM_ReadBuffer(unsigned int addr, unsigned char *buffer, unsigned int len);
void EEPROM_ClearAll(void);

void EEPROM_WriteBuffer(unsigned int addr, unsigned char *buffer, unsigned int len);

// 8位
signed char EEPROM_ReadS8(unsigned int addr);
void        EEPROM_WriteS8(unsigned int addr, signed char val);
unsigned char EEPROM_ReadU8(unsigned int addr);
void          EEPROM_WriteU8(unsigned int addr, unsigned char val);

// 16位 (int / short)
signed int   EEPROM_ReadS16(unsigned int addr);
void         EEPROM_WriteS16(unsigned int addr, signed int val);
unsigned int EEPROM_ReadU16(unsigned int addr);
void         EEPROM_WriteU16(unsigned int addr, unsigned int val);

// 32位 (long)
signed long   EEPROM_ReadS32(unsigned int addr);
void          EEPROM_WriteS32(unsigned int addr, signed long val);
unsigned long EEPROM_ReadU32(unsigned int addr);
void          EEPROM_WriteU32(unsigned int addr, unsigned long val);

// 浮点 (float, 32位)
float EEPROM_ReadFloat(unsigned int addr);
void  EEPROM_WriteFloat(unsigned int addr, float val);

#endif // __24C64_H__