#include "uart.h"


void UART_Init(void) //11.0592MHz  9600bps 
{
    SCON  = 0x50;    
    PCON &= 0x7F;    
    AUXR &= 0xBF;   
    AUXR &= 0xFE;    
    TMOD &= 0x0F;    
    TMOD |= 0x20;    
    TL1 = 0xFD;      
    TH1 = 0xFD;      
    ET1 = 0;         // 禁止T1中断
    ES  = 0;         // 禁止串口中断（查询发送）
    TR1 = 1;         // 启动T1
    TI  = 1;         


    UART_SendByte(0x55);
    UART_SendByte(0x55);
    UART_SendByte(0x55);
    UART_SendByte('\r');
    UART_SendByte('\n');
}

void UART_SendByte(uint8_t byte)
{
    TI = 0;         // 先清零TI
    SBUF = byte;    // 写缓冲启动发送
    while (!TI);    // 等待发送结束（硬件置TI=1）
}

void UART_SendString(uint8_t *str)
{
	while(*str)
		UART_SendByte(*str++);
}

void UART_SendHex(uint8_t byte)
{
	uint8_t high, low;
	high = byte >> 4;
	low = byte & 0x0F;
	if(high < 10) UART_SendByte('0' + high);
	else UART_SendByte('A' + (high - 10));
	if(low < 10) UART_SendByte('0' + low);
	else UART_SendByte('A' + (low - 10));
}

/* 打印 uint16 十进制 */
void UART_SendU16(uint16_t val)
{
    uint8_t buf[5], i = 0;
    if (val == 0) { UART_SendByte('0'); return; }
    while (val > 0 && i < 5) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) UART_SendByte(buf[--i]);
}

/* 打印 uint32 十进制 */
void UART_SendU32(uint32_t val)
{
    uint8_t buf[10], i = 0;
    if (val == 0) { UART_SendByte('0'); return; }
    while (val > 0 && i < 10) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) UART_SendByte(buf[--i]);
}

/* 打印 float (保留2位小数) */
void UART_SendFloat2(float val)
{
    uint16_t int_part;
    uint8_t frac;
    if (val < 0) { UART_SendByte('-'); val = -val; }
    int_part = (uint16_t)val;
    UART_SendU16(int_part);
    UART_SendByte('.');
    frac = (uint8_t)((val - int_part) * 100.0f + 0.5f);
    UART_SendByte('0' + (frac / 10));
    UART_SendByte('0' + (frac % 10));
}