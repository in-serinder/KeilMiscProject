#include <reg52.h>
#include <Ser.h>
#include <Timer.h>

#ifndef UTIL_H
#define UTIL_H

typedef struct UtilOBJ
{
    char Time[7];
    char Date[11];
    unsigned char Temperature[6];
    unsigned char Humidity[6];
    unsigned char Relay1 : 1; // ?1?
    unsigned char Relay2 : 1; // ?1?
    unsigned char Relay3 : 1; // ?1?
    unsigned char WarnFlag : 1;
    unsigned char reserved : 4;
} Util;

struct UtilOBJ getUtil(char *str);
void InitExternalInterrupt0();
bit getIRFlag();
unsigned char *getIRData();
void removeChars(char *str, const char *targets);
void RelayBuzzer();
unsigned int charToint(unsigned char ch);
// void insertCharInString(char *str, unsigned char pos, char ch);

#endif