#include "stc15w.h"
#include "Delay.h"
#include "ADC.h"

sbit LED_ent = P1^2;
sbit LED_A0 = P1^3;
sbit LED_A1 = P1^4;
sbit LED_A2 = P1^5;

sbit KEY_CLEAR = P3^3;
sbit KEY_DS = P3^2;

uint8_t ds[2];
uint8_t dsnum;


void set74HC138(unsigned char num)
{
    LED_A0 = num & 0x01;    
    LED_A1 = (num >> 1) & 0x01; 
    LED_A2 = (num >> 2) & 0x01;  
}


void INT0_ISR(void) interrupt 0
{

    Delay1us();
    if(KEY_DS == 0){
		if(dsnum>=2)dsnum=0;
       ds[dsnum]=getRandomNum();
			LED_ent =1;
			dsnum++;
    }
}

void INT1_ISR(void) interrupt 2
{
		
    Delay1us();
    if(KEY_CLEAR == 0){
	
			 ds[0]=9;
				ds[1]=9;
			dsnum=0;
       LED_ent =0;
 
    }
}


void Exti_Init(void)
{

    IT0 = 1;    
    EX0 = 1;    

    IT1 = 1;   
    EX1 = 1;   
    EA = 1;     
}



void main(void)
{
    unsigned char i = 0;  
  initADC();  
	Exti_Init();
	LED_ent=0;
	    ds[0] = 9;
    ds[1] = 9;
    dsnum = 0;
    while(1)  
    {
		for(i=0;i<2;i++){
			set74HC138(ds[i]);
			Delay1ms();
		}
			
    }
}