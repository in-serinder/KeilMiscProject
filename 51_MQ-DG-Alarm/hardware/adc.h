#ifndef __ADC_H__
#define __ADC_H__

#include "board.h"

/* STC8G 内置 10 位 ADC（STC8G1K08A：6 通道，满量程 1023）
 *
 * 通道号（手册 16.1.1 表，STC8G1K08A）：0=P3.0  1=P3.1  2=P3.2  3=P3.3
 *                                    4=P5.4  5=P5.5  15=内部 1.19V 基准
 * ★ STC8G1K08-8Pin（不带 A）没有 ADC 模块，ADC_CONTR/ADCCFG 都是不存在的 SFR，
 *   写进去读回 0，转换永远不完成。ADC_Init 会回读自检，上层据此显示 "noAd"。 */

void     ADC_Init(void);                 /* 上电 + 时序/格式配置，并自检 ADC 是否存在 */

#if !CFG_MQ_DIGITAL
/* 1 = 片上 ADC 确实能转换；0 = 试了两套配置都转不动 */
uint8_t  ADC_IsPresent(void);
#endif

#if CFG_MQ_AUTO_DO || CFG_MQ_DIGITAL
/* 1 = 当前走数字 DO（要么强制 CFG_MQ_DIGITAL=1，要么 ADC 自检失败后自动降级） */
uint8_t  ADC_IsDigital(void);
#endif

#if CFG_DEBUG_UART && !CFG_MQ_DIGITAL
/* 实际生效的 ADCCFG（0x2F = 官方推荐；0x0F = 退一步的左对齐方案） */
uint8_t  ADC_GetCfg(void);

/* 开机寄存器自检结果：0=ADC_CONTR(写 0x0A) 1=ADC_CONTR(写 0x82 后) 
 *                    2=ADCCFG  3=ADCTIM  4=P3M1 5=IE2 6=WDT_CONTR 7=CLKDIV */
uint8_t  ADC_ProbeReg(uint8_t idx);
#endif

/* 读一次指定通道，返回 0~1023；转换超时（ADC 没工作）返回 0xFFFF。 */
uint16_t ADC_RawRead(uint8_t ch);

void     ADC_SetCh(uint8_t ch);          /* 指定后续 MQ_Read 用哪个通道 */

/* 采集 MQ：丢首样 + 最多 8 次平均，返回 0~1023；
 * 全部超时返回 0xFFFF（与"真的是 0V"区分开）。 */
uint16_t MQ_Read(void);

#if CFG_DEBUG_UART
/* 诊断用（仅串口调试时需要）：上一次 MQ_Read 的超时次数、上次转换的空转次数 */
uint8_t  ADC_GetCh(void);
uint8_t  ADC_LastTo(void);
uint16_t ADC_LastGuard(void);
#endif

#endif /* __ADC_H__ */
