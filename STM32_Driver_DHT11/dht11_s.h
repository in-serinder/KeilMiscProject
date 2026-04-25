#ifndef __DHT11_S_H
#define __DHT11_S_H
#include "stm32f10x.h"
// 切换推挽
#define DHT11_MODE_OUT() do{\
    GPIO_InitTypeDef GPIO_InitStructure;\
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;\
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;\
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; \
    GPIO_Init(GPIOB, &GPIO_InitStructure);\
} while (0)

//切换上拉输入
#define DHT11_MODE_IN() do{\
    GPIO_InitTypeDef GPIO_InitStructure;\
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;\
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;\
    GPIO_Init(GPIOB, &GPIO_InitStructure);\
} while (0)



void rstDHT11_S(void);
uint8_t init_DHT11_S(void);
uint8_t checkDHT11_S(void);
uint8_t readDHT11_S(void);
uint8_t readDHT11_S_Byte(void);
uint8_t DHT11_S_Read(uint8_t *temperature, uint8_t *humidity);

#endif // __DHT11_S_H