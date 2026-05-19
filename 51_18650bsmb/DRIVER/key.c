#include "key.h"

#define THRESHOLD_ADD P33  //int1
#define THRESHOLD_SUB P32  //int0

/**
 * @brief 按键初始化函数
 * @note 配置外部中断0(P32)和外部中断1(P33)为下降沿触发
 */
void KEY_Init(void)
{
    // 设置P32和P33为输入模式
    P3M0 &= ~(0x0C);  // P32、P33设置为高阻输入
    P3M1 |= 0x0C;
    
    // 配置外部中断触发方式为下降沿触发
    IT0 = 1;  // INT0下降沿触发
    IT1 = 1;  // INT1下降沿触发
    
    // 使能外部中断
    EX0 = 1;  // 使能INT0
    EX1 = 1;  // 使能INT1
    
    // 开总中断
    EA = 1;
}

/**
 * @brief HRESHOLD_SUB按键
 */
void EXTI0_IRQHandler(void) interrupt 0
{
    delay_ms(10);
    if(THRESHOLD_SUB == 0)
    {
        // TO DO: 添加按键处理逻辑
        // 阈值减少按键处理
    }
}

/**
 * @brief THRESHOLD_ADD按键
 */
void EXTI1_IRQHandler(void) interrupt 2
{
    delay_ms(10);
    if(THRESHOLD_ADD == 0)
    {
        // TO DO: 添加按键处理逻辑
        // 阈值增加按键处理
    }
}