#ifndef __UART_H__
#define __UART_H__

#include "stc89c52.h"

void UART_Init(void);
void UART_SendByte(uint8_t byte);
void UART_SendString(uint8_t *str);
void UART_SendHex(uint8_t byte);
void UART_SendU16(uint16_t val);
void UART_SendU32(uint32_t val);
void UART_SendFloat2(float val);

// 格式: [Debug] [类型] 内容
// 类型: UO=用户操作, RW=读写操作, DP=显示刷新, AD=ADC读取, SY=系统

#define DEBUG_TAG "[Debug] "

#define DEBUG_SY(tag, fmt) \
  do { UART_SendString(DEBUG_TAG "[SY] " tag ": " fmt "\r\n"); } while(0)

#define DEBUG_UO(tag, fmt) \
  do { UART_SendString(DEBUG_TAG "[UO] " tag ": " fmt "\r\n"); } while(0)

#define DEBUG_RW(tag, fmt) \
  do { UART_SendString(DEBUG_TAG "[RW] " tag ": " fmt "\r\n"); } while(0)

#define DEBUG_DP(tag, fmt) \
  do { UART_SendString(DEBUG_TAG "[DP] " tag ": " fmt "\r\n"); } while(0)

#define DEBUG_AD(tag, fmt) \
  do { UART_SendString(DEBUG_TAG "[AD] " tag ": " fmt "\r\n"); } while(0)

#endif // __UART_H__
