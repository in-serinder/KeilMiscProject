#include "key.h"

#define RESET_KEEP_TIME 6000 //6000ms保持RESET+MODE键按下 保证重置

uint16_t xdata Reset_Cnt = 0;

void Key_Init(void){
    IT1 = 0;			//INT1(P3.3)上升沿+下降沿中断
	EX1 = 1;			//使能INT1中断
}


bit Key_Scan(void){
    //对于模式检测处于主函数 这里主要是对重置里程键进行扫描
    if(Reset_Key&&Mode_Key){
        Reset_Cnt++;
    }
    else{
        Reset_Cnt = 0;
    }

    return Reset_Cnt >= RESET_KEEP_TIME?1:0;
}
