#include "delay.h"
#include "ir_receive.h"
#include "relay.h"
#include "stc15w.h"
#include "uart.h"

void main() {
  UART_Init();
  UART_SendString((uint8_t *)"Booting ....\r\n");

  /*初始化位*/
  init_relays();
  IR_Init();

  // [DBG] 00 FF 30 CF Key:0x30
  // Unknown Key
  // [DBG] 00 FF 18 E7 Key:0x18
  // Running Light ON
  // [DBG] 00 FF 7A 85 Key:0x7A
  // Unknown Key
  // [DBG] 00 FF 10 EF Key:0x10
  // Unknown Key
  // [DBG] 00 FF 90 6F Key:0x90
  // Unknown Key
  // [DBG] 00 FF A8 57 Key:0xA8
  // Unknown Key
  // [DBG] 00 FF 02 FD Key:0x02
  // Unknown Key
  // [DBG] 00 FF C2 3D Key:0xC2
  // Unknown Key

  /*自检*/
  UART_SendString((uint8_t *)"Booted Relay Self-Checking ....\r\n");
  Delay_ms(500);
  Relay_self_checking_relays();
  UART_SendString((uint8_t *)"Relay Self-Checking Completed.\r\n");
  Relay_set_ao_led();
  while (1) {
    Relay_set_ao_led();
    if (ir_receive_complete) {
      uint8_t key = IR_GetKeyCode();

      // 调试：打印红外原始数据
      UART_SendString((uint8_t *)"[DBG] ");
      UART_SendHex(ir_raw_data[0]);
      UART_SendByte(' ');
      UART_SendHex(ir_raw_data[1]);
      UART_SendByte(' ');
      UART_SendHex(ir_raw_data[2]);
      UART_SendByte(' ');
      UART_SendHex(ir_raw_data[3]);
      UART_SendString((uint8_t *)" Key:0x");
      UART_SendHex(key);
      UART_SendString((uint8_t *)"\r\n");

      // 按键功能映射（直接使用按键编码）
      switch (key) {
      case 0x30: // CH1 切换
        Relay_toggle(1);
        UART_SendString((uint8_t *)"Toggled Relay 1\n");
        break;
      case 0x18: // CH2 切换
        Relay_toggle(2);
        UART_SendString((uint8_t *)"Toggled Relay 2\n");
        break;
      case 0x7A: // CH3 切换
        Relay_toggle(3);
        UART_SendString((uint8_t *)"Toggled Relay 3\n");
        break;
      case 0x10: // CH4 切换
        Relay_toggle(4);
        UART_SendString((uint8_t *)"Toggled Relay 4\n");
        break;
      case 0x90: // 上键：关闭所有 → 流水灯开启
      case 0xA8: // 下键：关闭所有 → 流水灯开启
        UART_SendString((uint8_t *)"Running Light ON\n");
        Relay_running_light_on();
        break;
      case 0x02: // 左键：开启所有 → 流水灯关闭
      case 0xC2: // 右键：开启所有 → 流水灯关闭
        UART_SendString((uint8_t *)"Running Light OFF\n");
        Relay_running_light_off();
        break;
      case 0x22: // 翻转所有继电器状态
        Relay_toggle_all();
        UART_SendString((uint8_t *)"Toggled All Relays\n");
        break;
      default:
        UART_SendString((uint8_t *)"Unknown Key\n");
        break;
      }
      UART_SendHex(Relay_get_status());
    }
  }
}