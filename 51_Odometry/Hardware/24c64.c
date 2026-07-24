#include "24c64.h"

// 延时函数
void I2C_Delay(void)
{
    // unsigned char i = 2;
    unsigned char i = 100; // 低于100延时在解码中会出现多1字符解码问题（逻辑分析仪显示FF） 或许加个电阻同步阻抗会很好
    while (i--)
        ;
}

// 初始化I2C总线
void I2C_Init(void)
{
    SDA = 1;
    SCL = 1;
}

// 发送起始信号
void I2C_Start(void)
{
    SDA = 1;
    SCL = 1;
    I2C_Delay();
    SDA = 0;
    I2C_Delay();
    SCL = 0;
}

// 发送停止信号
void I2C_Stop(void)
{
    SDA = 0;
    SCL = 1;
    I2C_Delay();
    SDA = 1;
    I2C_Delay();
}

// 发送一个字节
void I2C_SendByte(unsigned char byte)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        SDA = byte & 0x80;
        byte <<= 1;
        SCL = 1;
        I2C_Delay();
        SCL = 0;
        I2C_Delay();
    }
}

// 接收一个字节
unsigned char I2C_ReceiveByte(void)
{
    unsigned char i, byte = 0;
    SDA = 1;
    for (i = 0; i < 8; i++)
    {
        byte <<= 1;
        SCL = 1;
        I2C_Delay();
        byte |= SDA;
        SCL = 0;
        I2C_Delay();
    }
    return byte;
}

// 等待应答
bit I2C_WaitAck(void)
{
    bit ack;
    SDA = 1;
    I2C_Delay();
    SCL = 1;
    I2C_Delay();
    ack = SDA;
    SCL = 0;
    I2C_Delay();
    return ack;
}

// 发送应答
void I2C_SendAck(bit ack)
{
    SDA = ack;
    I2C_Delay();
    SCL = 1;
    I2C_Delay();
    SCL = 0;
    I2C_Delay();
}

// 向EEPROM写入一个字节
void EEPROM_WriteByte(unsigned int addr, unsigned char dat)
{
    unsigned char wait_i = 255;
    I2C_Start();
    I2C_SendByte(EEPROM_ADDR);
    if (I2C_WaitAck())
        return;
    I2C_SendByte(addr >> 8);
    if (I2C_WaitAck())
        return;
    I2C_SendByte(addr & 0xFF);
    if (I2C_WaitAck())
        return;
    I2C_SendByte(dat);
    if (I2C_WaitAck())
        return;
    I2C_Stop();
    // 等待写入完成

    while (wait_i--)
        ;
    ;
}

// 从EEPROM读取一个字节
unsigned char EEPROM_ReadByte(unsigned int addr)
{
    unsigned char dat;
    I2C_Start();
    I2C_SendByte(EEPROM_ADDR);
    if (I2C_WaitAck())
        return 0;
    I2C_SendByte(addr >> 8);
    if (I2C_WaitAck())
        return 0;
    I2C_SendByte(addr & 0xFF);
    if (I2C_WaitAck())
        return 0;
    I2C_Start();
    I2C_SendByte(EEPROM_ADDR | 0x01);
    if (I2C_WaitAck())
        return 0;
    dat = I2C_ReceiveByte();
    I2C_SendAck(1);
    I2C_Stop();
    return dat;
}

// 向EEPROM写入字符串
void EEPROM_WriteString(unsigned int addr, unsigned char *str)
{
    while (*str)
    {
        EEPROM_WriteByte(addr++, *str++);
    }
    EEPROM_WriteByte(addr, 0); // 写入结束符
}

// 从EEPROM读取字符串
void EEPROM_ReadString(unsigned int addr, unsigned char *str, unsigned int len)
{
    unsigned int read_str_i;
    for (read_str_i = 0; read_str_i < len; read_str_i++)
    {
        *str = EEPROM_ReadByte(addr++);
        if (*str == 0)
            break;
        str++;
    }
    *str = 0; // 确保字符串结束
}

// 读取EEPROM指定长度内容
void EEPROM_ReadBuffer(unsigned int addr, unsigned char *buffer, unsigned int len)
{
    unsigned int read_i;
    for (read_i = 0; read_i < len; read_i++)
    {
        buffer[read_i] = EEPROM_ReadByte(addr + read_i);
    }
}

// 清空EEPROM全部内容
void EEPROM_ClearAll(void)
{
    unsigned int clear_i;
    for (clear_i = 0; clear_i < 8192; clear_i++) // 24C64容量为8KB
    {
        EEPROM_WriteByte(clear_i, 0);
    }
}

// ========== 通用 Buffer 读写 ==========

/**
 * @brief  向EEPROM写入一段数据（通用buffer）
 * @param  addr   起始地址
 * @param  buffer 数据缓冲区指针
 * @param  len    写入字节数
 */
void EEPROM_WriteBuffer(unsigned int addr, unsigned char *buffer, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++)
    {
        EEPROM_WriteByte(addr + i, buffer[i]);
    }
}

// ========== 类型化读写（short / int / long / float） ==========

// 利用 union 共享内存进行字节拆分/合并，避免指针强转的严格别名问题
typedef union {
    unsigned char  u8;
    signed   char  s8;
    unsigned int   u16;
    signed   int   s16;
    unsigned long  u32;
    signed   long  s32;
    float          f32;
    unsigned char  bytes[4]; // 最大 4 字节，覆盖 long/float
} EEPROM_Union_t;

// --- unsigned char / signed char ---
void EEPROM_WriteU8(unsigned int addr, unsigned char val)
{
    EEPROM_WriteByte(addr, val);
}
unsigned char EEPROM_ReadU8(unsigned int addr)
{
    return EEPROM_ReadByte(addr);
}

void EEPROM_WriteS8(unsigned int addr, signed char val)
{
    EEPROM_WriteByte(addr, (unsigned char)val);
}
signed char EEPROM_ReadS8(unsigned int addr)
{
    return (signed char)EEPROM_ReadByte(addr);
}

// --- unsigned int / signed int (16位) ---
void EEPROM_WriteU16(unsigned int addr, unsigned int val)
{
    EEPROM_Union_t u;
    u.u16 = val;
    EEPROM_WriteBuffer(addr, u.bytes, 2);
}
unsigned int EEPROM_ReadU16(unsigned int addr)
{
    EEPROM_Union_t u;
    EEPROM_ReadBuffer(addr, u.bytes, 2);
    return u.u16;
}

void EEPROM_WriteS16(unsigned int addr, signed int val)
{
    EEPROM_Union_t u;
    u.s16 = val;
    EEPROM_WriteBuffer(addr, u.bytes, 2);
}
signed int EEPROM_ReadS16(unsigned int addr)
{
    EEPROM_Union_t u;
    EEPROM_ReadBuffer(addr, u.bytes, 2);
    return u.s16;
}

// --- unsigned long / signed long (32位) ---
void EEPROM_WriteU32(unsigned int addr, unsigned long val)
{
    EEPROM_Union_t u;
    u.u32 = val;
    EEPROM_WriteBuffer(addr, u.bytes, 4);
}
unsigned long EEPROM_ReadU32(unsigned int addr)
{
    EEPROM_Union_t u;
    EEPROM_ReadBuffer(addr, u.bytes, 4);
    return u.u32;
}

void EEPROM_WriteS32(unsigned int addr, signed long val)
{
    EEPROM_Union_t u;
    u.s32 = val;
    EEPROM_WriteBuffer(addr, u.bytes, 4);
}
signed long EEPROM_ReadS32(unsigned int addr)
{
    EEPROM_Union_t u;
    EEPROM_ReadBuffer(addr, u.bytes, 4);
    return u.s32;
}

// --- float (32位) ---
void EEPROM_WriteFloat(unsigned int addr, float val)
{
    EEPROM_Union_t u;
    u.f32 = val;
    EEPROM_WriteBuffer(addr, u.bytes, 4);
}
float EEPROM_ReadFloat(unsigned int addr)
{
    EEPROM_Union_t u;
    EEPROM_ReadBuffer(addr, u.bytes, 4);
    return u.f32;
}