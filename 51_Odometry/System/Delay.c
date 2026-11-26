#include "Delay.h"
#include <intrins.h>

/* STC-ISP 按 11.0592MHz / 12T 生成的 1ms 延时（i=11, j=190）。
 * 注意别改成 i=2/j=199 —— 那是 1T 模式的值，在 12T 下会变成 4~5ms，
 * 整个系统（按键长按、显示刷新节拍）都会跟着慢 4 倍。 */
void Delay1ms(void)	//@11.0592MHz 12T
{
	unsigned char data i, j;

	_nop_();
	_nop_();
	_nop_();
	i = 11;
	j = 190;
	do
	{
		while (--j);
	} while (--i);
}


void Delay_ms(uint16_t ms) {
  while (ms--) {
    Delay1ms();
  }
}

/* 粗略延时：每次循环约 10 个机器周期（12T 下约 11us）。
 * 只用于 MAX7219 这类"只要够慢就行"的时序，不追求精度。 */
void Delay_us(uint16_t us)	//@11.0592MHz 12T
{
  while(us--){
	_nop_();
    _nop_();
  }

}