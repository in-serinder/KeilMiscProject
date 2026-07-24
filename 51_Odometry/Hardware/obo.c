#include "obo.h"

//这里是通过光电模块对转轴上的触发板进行触发采集 当转一圈后通过轮子周长计数
#define OBO_RATE 1000 //比例系数 这里是通过 轮子周长(cm) 获得公里就是 总程+OBO_RATE
#define MILES_WRITE_COUNT 30 //达到指定计数周期后写入到EEPROM中
#define MILES_ADDR 0x01 //存储里程的地址

uint32_t idata befor_Miles = 0; //公里数
uint32_t idata millis_cm = 0; //cm制
uint8_t idata write_count = 0; //写入计数周期
float idata speed_kmh = 0.0f; //速度

void OBO_Init(void){
   	IT0 = 0;			//INT0(P3.2)上升沿+下降沿中断
	EX0 = 1;			//使能INT0中断

    Delay_ms(10);
    // 从EEPROM中读取里程
    millis_cm = EEPROM_ReadU32(MILES_ADDR);
    
}

void OBO_SET_Millis(uint32_t ms){
    millis_cm = ms;
    write_count = 0;
    EEPROM_WriteU32(MILES_ADDR, millis_cm);
}

float OBO_GET_Miles(void){
   return millis_cm / 1000.0; //返回公里数
}


//必须1s内调用一次 通过s级获取速度
float OBO_GET_SPEED(void){

    float speed_ms = 0.0f;
    // float speed_kmh = 0.0f;

    // 1秒路程(cm) 转换为 米/秒(m/s)
    speed_ms = (millis_cm - befor_Miles) / 100.0f;
    // m/s 换算 公里每小时 km/h
    speed_kmh = speed_ms * 3.6f;

    //上次访问点
    befor_Miles = millis_cm;

    // 防抖
    if(speed_kmh < 1.0f)
        speed_kmh = 0.0f;



    return speed_kmh;
}


void INT0_Isr(void) interrupt 0
{
    millis_cm += OBO_RATE;
    if(write_count >= MILES_WRITE_COUNT){
        EEPROM_WriteU32(MILES_ADDR, millis_cm);
        write_count = 0;
    }
    write_count++;
}