#ifndef __UART_H__
#define __UART_H__

#include "stc89c52.h"

/* 调试输出总开关。
 * 0 = 全部折叠成空语句（省约 1KB Flash，8K 才装得下串口命令功能）
 * 1 = 打开后才能用 DEBUG_xx / UART_SendFloat2 排版打印 */
#define UART_DEBUG 0

void UART_Init(void);
void UART_Poll(void);

#if UART_DEBUG
void UART_SendByte(uint8_t byte);
void UART_SendString(uint8_t *str);
void UART_SendU16(uint16_t val);
void UART_SendU32(uint32_t val);
void UART_SendFloat2(float val);
#endif

// 格式: [Debug] [类型] 内容
// 类型: UO=用户操作, RW=读写操作, DP=显示刷新, AD=ADC读取, SY=系统

#define DEBUG_TAG "[Debug] "

#if UART_DEBUG

#define DEBUG_SY(tag, fmt) \
  do { UART_SendString((uint8_t *)(DEBUG_TAG "[SY] " tag ": " fmt "\r\n")); } while(0)
#define DEBUG_UO(tag, fmt) \
  do { UART_SendString((uint8_t *)(DEBUG_TAG "[UO] " tag ": " fmt "\r\n")); } while(0)
#define DEBUG_RW(tag, fmt) \
  do { UART_SendString((uint8_t *)(DEBUG_TAG "[RW] " tag ": " fmt "\r\n")); } while(0)
#define DEBUG_DP(tag, fmt) \
  do { UART_SendString((uint8_t *)(DEBUG_TAG "[DP] " tag ": " fmt "\r\n")); } while(0)
#define DEBUG_AD(tag, fmt) \
  do { UART_SendString((uint8_t *)(DEBUG_TAG "[AD] " tag ": " fmt "\r\n")); } while(0)

#else

#define DEBUG_SY(tag, fmt)   do {} while(0)
#define DEBUG_UO(tag, fmt)   do {} while(0)
#define DEBUG_RW(tag, fmt)   do {} while(0)
#define DEBUG_DP(tag, fmt)   do {} while(0)
#define DEBUG_AD(tag, fmt)   do {} while(0)

#endif

#endif // __UART_H__
