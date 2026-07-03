#include "delay.h"
#include "ir_receive.h"
#include "relay.h"
#include "stc15w.h"
#include "uart.h"

void main() {
  uint8_t cmd_flag = 0;
  uint8_t digit;

  /*初始化位*/
  init_relays();
  IR_Init();
  UART_Init();
  /*自检*/
  UART_SendString((uint8_t *)"Booted Relay Self-Checking ....");
  Delay_ms(500);
  Relay_self_checking_relays();
  UART_SendString((uint8_t *)"Relay Self-Checking Completed.");
  Relay_set_ao_led();
  while (1) {
    Relay_set_ao_led();
    if (ir_receive_complete) {
      digit = IR_GetDigit();
      if (digit <= 4) // 仅处理1-4的数字按键
      {
        Relay_toggle(digit); // 切换对应继电器状态
        UART_SendString((uint8_t *)"Toggled Relay ");
        cmd_flag = Relay_ir_key_code_map_to_channel(digit);
        if (cmd_flag != 0)
          UART_SendString((uint8_t *)"Unknown Command\n");

        UART_SendByte('0' + digit); // 发送继电器编号
        UART_SendByte('\n');
      } else {
        UART_SendString((uint8_t *)"Received Non-Digit Key\n");
      }
    }
  }
}