#ifndef RELAY_H
#define RELAY_H
#include "delay.h"
#include "stc15w.h"

#define CH1_RELAY P33
#define CH2_RELAY P36
#define CH3_RELAY P37
#define CH4_RELAY P10

#define RELAY_ALLOPEN P12

#define RELAY_IO_PUSH_PULL()                                                   \
  do {                                                                         \
    P3M1 &= ~(0x08 | 0x40 | 0x80);                                             \
    P3M0 |= (0x08 | 0x40 | 0x80);                                              \
    P1M1 &= ~(0x01 | 0x04);                                                    \
    P1M0 |= (0x01 | 0x04);                                                     \
  } while (0)

void init_relays();
void Relay_set_state(uint8_t channel, uint8_t state);
void Relay_toggle(uint8_t channel);
void Relay_turn_off_all_relays();
void Relay_turn_on_all_relays();
void Relay_self_checking_relays();
void Relay_running_light_on(void);
void Relay_running_light_off(void);
void Relay_toggle_all(void);
uint8_t Relay_get_status();
void Relay_set_ao_led();
uint8_t Relay_ir_key_code_map_to_channel(uint8_t key_code);

#endif // RELAY_H