#include "relay.h"

void init_relays() {
  RELAY_IO_PUSH_PULL();
  CH1_RELAY = 0; // Initialize all relays to off
  CH2_RELAY = 0;
  CH3_RELAY = 0;
  CH4_RELAY = 0;
}

void Relay_set_state(uint8_t channel, uint8_t state) {
  switch (channel) {
  case 1:
    CH1_RELAY = state;
    break;
  case 2:
    CH2_RELAY = state;
    break;
  case 3:
    CH3_RELAY = state;
    break;
  case 4:
    CH4_RELAY = state;
    break;
  default:
    // Invalid channel, handle error if necessary
    break;
  }
}

void Relay_toggle(uint8_t channel) {
  switch (channel) {
  case 1:
    CH1_RELAY = !CH1_RELAY;
    break;
  case 2:
    CH2_RELAY = !CH2_RELAY;
    break;
  case 3:
    CH3_RELAY = !CH3_RELAY;
    break;
  case 4:
    CH4_RELAY = !CH4_RELAY;
    break;
  default:
    // Invalid channel, handle error if necessary
    break;
  }
}

void Relay_turn_off_all_relays() {
  CH1_RELAY = 0;
  CH2_RELAY = 0;
  CH3_RELAY = 0;
  CH4_RELAY = 0;
}

void Relay_turn_on_all_relays() {
  CH1_RELAY = 1;
  CH2_RELAY = 1;
  CH3_RELAY = 1;
  CH4_RELAY = 1;
}

// 流水灯：逐个开启（关闭所有 → 逐个打开）
void Relay_running_light_on(void) {
  uint8_t i;
  Relay_turn_off_all_relays();
  for (i = 1; i <= 4; i++) {
    Relay_set_state(i, 1);
    Delay_ms(300);
  }
}

// 流水灯：逐个关闭（打开所有 → 逐个关闭）
void Relay_running_light_off(void) {
  uint8_t i;
  Relay_turn_on_all_relays();
  for (i = 4; i >= 1; i--) {
    Relay_set_state(i, 0);
    Delay_ms(300);
  }
}

// 翻转所有继电器状态（开→关，关→开）
void Relay_toggle_all(void) {
  CH1_RELAY = !CH1_RELAY;
  CH2_RELAY = !CH2_RELAY;
  CH3_RELAY = !CH3_RELAY;
  CH4_RELAY = !CH4_RELAY;
}

void Relay_self_checking_relays() {
  uint8_t i = 0;
  // 流水检测所有继电器
  for (i = 1; i <= 4; i++) {
    Relay_set_state(i, 1); // Turn on relay
    Delay_ms(500);         // Wait for 500ms
    Relay_set_state(i, 0); // Turn off relay
    Delay_ms(500);         // Wait for 500ms
  }

  for (i = 4; i >= 1; i--) {
    Relay_set_state(i, 1); // Turn on relay
    Delay_ms(500);         // Wait for 500ms
    Relay_set_state(i, 0); // Turn off relay
    Delay_ms(500);         // Wait for 500ms
  }
  // 开关全部
  Relay_turn_on_all_relays();
  Delay_ms(500);
  Relay_turn_off_all_relays();
  Delay_ms(500);
}

uint8_t Relay_get_status(void) {
  uint8_t status = 0;

  if (CH1_RELAY)
    status |= (1 << 0);
  if (CH2_RELAY)
    status |= (1 << 1);
  if (CH3_RELAY)
    status |= (1 << 2);
  if (CH4_RELAY)
    status |= (1 << 3);

  return status;
}
void Relay_set_ao_led() {
  if (Relay_get_status() == 0x0F)
    RELAY_ALLOPEN = 1;
}

// 红外编码映射功能
uint8_t Relay_ir_key_code_map_to_channel(uint8_t key_code) {
  switch (key_code) {
  case 0:
    Relay_toggle(1);
    return 0;
  case 1:
    Relay_set_state(2, !CH2_RELAY);
    return 0;
  case 2:
    Relay_set_state(3, !CH3_RELAY);
    return 0;
  case 3:
    Relay_set_state(4, !CH4_RELAY);
    return 0;
    break;
  // 其他指令处理
  // 全开
  case 4:
    Relay_turn_on_all_relays();
    return 0;
  // 全关
  case 5:
    Relay_turn_off_all_relays();
    return 0;
  // 再次自检
  case 6:
    Relay_self_checking_relays();
    return 0;
  default:
    return 1;
  }
}