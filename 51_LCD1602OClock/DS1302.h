#include <REGX52.H>

#ifdef DS1302_H
#define DS1302_H

char *getTimerString();

typedef struct DS1302
{
    char Time[17];
} DS1302;

// struct Time