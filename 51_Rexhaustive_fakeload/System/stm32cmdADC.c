#include "stm32cmdADC.h"
#include "UART.h"

// Keil C51不支持stdint.h，使用原生类型
// 接收缓冲区（直接解析，无需额外拷贝）
#define RX_BUF_SIZE 32
static unsigned char idata rx_buf[RX_BUF_SIZE];
static unsigned char rx_idx;

// ============================================================
// 以下主动发送指令的函数已注释，改用UART中断被动接收STM32数据
// ============================================================
// static unsigned char UART_ReceiveChar(unsigned int timeout) {
//  while (timeout--) {
//    if (RI) {
//      RI = 0;
//      return SBUF;
//    }
//  }
//  return 0;
//}
//
///**
// * @brief  发送命令到 STM32，等待回复，结果存于 rx_buf
// * @param  cmd  命令字符串（不含换行符）
// * @return 1=成功收到回复，0=超时
// */
// static unsigned char STM32_SendCommand(const char *cmd) {
//  unsigned char i;
//  unsigned char ch;
//
//  rx_idx = 0;
//  for (i = 0; i < RX_BUF_SIZE; i++)
//    rx_buf[i] = 0;
//
//  UART_SendString((unsigned char *)cmd);
//  UART_SendString("\r\n");
//
//  for (i = 0; i < STM32_CMD_TIMEOUT; i++) {
//    ch = UART_ReceiveChar(1);
//    if (ch != 0) {
//      if (rx_idx < RX_BUF_SIZE - 1)
//        rx_buf[rx_idx++] = ch;
//      if (ch == '\n') {
//        rx_buf[rx_idx] = '\0';
//        break;
//      }
//    }
//  }
//
//  return (rx_idx > 0 && rx_buf[0] == '>') ? 1 : 0;
//}
//
///**
// * @brief  从 rx_buf 解析 ">> XXXX" 格式的整数
// * @return 解析到的整数值
// */
// static unsigned int parse_uint(void) {
//  unsigned char i = 3; // 跳过 ">> "
//  unsigned int val = 0;
//  while (rx_buf[i] >= '0' && rx_buf[i] <= '9') {
//    val = val * 10 + (rx_buf[i] - '0');
//    i++;
//  }
//  return val;
//}
//
///**
// * @brief  从 rx_buf 解析 ">> XX.XX" 格式的浮点数
// * @return 解析到的浮点值
// */
// static float parse_float(void) {
//  unsigned char i = 3; // 跳过 ">> "
//  unsigned int int_part = 0;
//  unsigned int frac_part = 0;
//  unsigned int frac_div = 1;
//
//  while (rx_buf[i] >= '0' && rx_buf[i] <= '9') {
//    int_part = int_part * 10 + (rx_buf[i] - '0');
//    i++;
//  }
//  if (rx_buf[i] == '.') {
//    i++;
//    while (rx_buf[i] >= '0' && rx_buf[i] <= '9') {
//      frac_part = frac_part * 10 + (rx_buf[i] - '0');
//      frac_div *= 10;
//      i++;
//    }
//  }
//  return (float)int_part + (float)frac_part / (float)frac_div;
//}
//
///**
// * @brief  查询 STM32 原始 ADC 值
// * @return ADC 原始值（0-4095），超时返回 0xFFFF
// */
// unsigned int STM32_ReadADC(void) {
//  if (!STM32_SendCommand("AT+QUERY+ADC"))
//    return 0xFFFF;
//  return parse_uint();
//}
//
///**
// * @brief  查询 STM32 分压后输入电压
// * @return 电压值（伏特），超时返回 -1.0f
// */
// float STM32_ReadVoltage(void) {
//  if (!STM32_SendCommand("AT+QUERY+Voltage"))
//    return -1.0f;
//  return parse_float();
//}
// ===================== 注释结束 =====================

/**
 * @brief  初始化 STM32 通信
 */
void STM32_Init(void) {
  UART_SendString("STM32_ADC Helper: Querying STM32...\r\n");
}

/**
 * @brief  查询 STM32 分压后输入电压（后备函数，返回-1表示无数据）
 * @return 始终返回 -1.0f（改为纯中断接收，不再主动查询）
 */
float STM32_ReadVoltage(void) { return -1.0f; }