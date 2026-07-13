#ifndef __STM32CMD_ADC_H_
#define __STM32CMD_ADC_H_

// Keil C51不支持stdint.h，使用原生类型

#define STM32_CMD_TIMEOUT 5000 // 等待超时（大约500ms @ 9600bps）

/**
 * @brief  初始化 STM32 ADC 助手通信
 *         配置与 STM32 通信的 UART 引脚
 */
void STM32_Init(void);

/**
 * @brief  查询 STM32 原始 ADC 值
 * @return ADC 原始值（0-4095），超时返回 0xFFFF
 */
// unsigned int STM32_ReadADC(void);

/**
 * @brief  查询 STM32 分压后输入电压
 * @return 电压值（伏特），超时返回 -1.0f
//  */
float STM32_ReadVoltage(void);

#endif /* __STM32CMD_ADC_H_ */