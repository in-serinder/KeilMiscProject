#ifndef RELAY_H
#define RELAY_H
#include "stc89c52.h"
#include "delay.h"

#define CH1_RELAY P33
#define CH2_RELAY P36
#define CH3_RELAY P37
#define CH4_RELAY P10

#define RELAY_ALLOPEN P12

void init_relays();
void set_relay_state(unsigned char channel, unsigned char state);
void toggle_relay(unsigned char channel);
void turn_off_all_relays();
void trun_on_all_relays();
void self_checking_relays();
uint8_t get_relay_status();
void set_ao_led();
uint8_t ir_key_code_map_to_channel(uint8_t key_code);

#endif // RELAY_H