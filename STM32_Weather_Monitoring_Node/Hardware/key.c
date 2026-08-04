#include "key.h"
#include "Delay.h"

/*
PA15 照明LED按键
PB5 OLED屏幕显示按键
*/

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    /* PA15 是 JTAG_JTDI, 默认被JTAG调试接口占用
       必须先关闭JTAG(保留SWD)才能作为普通IO使用 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

enum KeyState Key_Read(void)
{
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15) == 0)
    {
        uint32_t wait = 0;
        Delay_ms(20);
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15) == 0)
        {
            while (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15) == 0 && wait < 500)
            {
                wait++;
                Delay_ms(1);
            }
            return Key_LIGHT_Power;
        }
    }
    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0)
    {
        uint32_t wait = 0;
        Delay_ms(20);
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0)
        {
            while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0 && wait < 500)
            {
                wait++;
                Delay_ms(1);
            }
            return Key_OLED_Power;
        }
    }
    return Key_None;
}