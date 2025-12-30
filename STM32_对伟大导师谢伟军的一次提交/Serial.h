#ifndef __SERIAL_H__
#define __SERIAL_H__



void Serial_init();
void Serial_sendByte(uint8_t Byte);
void Serial_sendArr(uint8_t *Array, uint16_t Length);
void Serial_sendStr(char *Str);

#endif
