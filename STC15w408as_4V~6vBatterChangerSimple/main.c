#include "stc15w.h"
#include "ADC.h"
#include "Delay.h"
#include "ChargerCTL.h"

/*
继电器控制默认低电平 P1.2
继电器切换P3.2中断控制

*/

sbit Relay = P1 ^ 2;
bit Relay_Status = 0;
int main()
{
	Relay = Relay_Status;
	initADC();

	startCharge6VBAT();
	startCharge4VBAT();
	while (1)
	{
		Delay1000ms();
		// Relay=!Relay;
		//	if(Relay){
		//	startCharge6VBAT();
		//		startCharge4VBAT();
		//	}else{
		//	stopCharge6VBAT();
		//		stopCharge4VBAT();
		//}
	}
}