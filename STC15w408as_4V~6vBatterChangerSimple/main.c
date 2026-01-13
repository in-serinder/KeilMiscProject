#include "stc15w.h"
#include "ADC.h"
#include "Delay.h"
#include "ChargerCTL.h"

void init_RelayCTRL(void);
/*
继电器控制默认低电平 P1.2
继电器切换P3.2中断控制

*/

sbit Relay = P1 ^ 2;
sbit Relay_CTRL = P3 ^ 2;
bit Relay_Status = 0;
bit stop6vCharge = 0;
int main()
{
	Relay = Relay_Status;
	initADC();
	init_RelayCTRL();

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
		if (getBatter_6V_AVG() <= 7.0 && (!stop6vCharge))
		{
			// 当对6v充电必须为低电平继电器电池连接控制
			// ups电源配置外部电源接入为第一次，内部adc检测后为第二次控制
			Relay = 0;
			Relay_Status = 0;
			startCharge6VBAT();
		}
		else if (getBatter_6V_AVG() >= 7.4)
		{
			stopCharge6VBAT();
		}

		if (getBatter_4V_AVG() <= 4.2)
		{
			startCharge4VBAT();
		}
		else if (getBatter_4V_AVG() >= 4.4)
		{
			stopCharge4VBAT();
		}

		Relay = Relay_Status;
	}
}

void init_RelayCTRL(void)
{
	P3M0 &= ~(1 << 2);
	P3M1 &= ~(1 << 2);
	IT0 = 1;
	EX0 = 1;
	EA = 1;
}

void INT0_ISR(void) interrupt 0
{

	Delay_ms(10);
	if (Relay_CTRL == 0)
	{
		Relay_Status = !Relay_Status;
		// 当处于串联继电器逻辑必须关闭6v充电
		if (Relay_Status)
		{
			stop6vCharge = 1;
			stopCharge6VBAT();
		}
		else if (!Relay_Status && getBatter_6V() < 7.6)
		{
			stop6vCharge = 0;
		}

		while (Relay_CTRL == 0)
			;
	}
}