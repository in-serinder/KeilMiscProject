#include "stc15w.h"

#ifndef __KEY_H__
#define __KEY_H__

/**
 * @brief 按键初始化函数
 * @note 配置外部中断0(P32)和外部中断1(P33)为下降沿触发
 */
void KEY_Init(void);

/**
 * @brief 外部中断0服务函数(THRESHOLD_SUB按键)
 */
//void EXTI0_IRQHandler(void) interrupt 0;

/**
 * @brief 外部中断1服务函数(THRESHOLD_ADD按键)
 */
//void EXTI1_IRQHandler(void) interrupt 2;

#endif