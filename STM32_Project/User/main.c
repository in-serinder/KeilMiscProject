#include"stm32f10x.h"

int main(void){
	//配置RCC寄存器使能时钟
	RCC->APB2ENR=0x00000010;

	//配置端口
	GPIOC->CRH=0x00300000;
	GPIOC->ODR=0x00002000;
	
	
while(1){

}

}

