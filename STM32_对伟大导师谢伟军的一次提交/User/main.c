#include"stm32f10x.h"
#include"Serial.h"

int main(void){
	//寄存器方式配置RCC寄存器使能时钟
	//RCC->APB2ENR=0x00000010;

	//寄存器方式配置端口
	//GPIOC->CRH=0x00300000;
	//GPIOC->ODR=0x00002000;
	
	
	//库函数方式使能时钟 配置端口
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE); //使能C端
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP; //推挽
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_All;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	
	GPIO_SetBits(GPIOA,GPIO_Pin_All);
//	GPIO_ResetBits(GPIOC,GPIO_Pin_13);
	
	Serial_init();
	Serial_sendStr("Start");
while(1){

}

}

