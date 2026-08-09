#ifndef __RAIN_S_H__
#define __RAIN_S_H__
#include "stm32f10x.h"
#include <stdbool.h>

/* ==================================================================
 *  雨滴传感器 (梳状覆铜板 叉指电极) 驱动配置
 *  硬件跳线可选两种接法, 通过 RAIN_SENSOR_POLARITY 切换:
 *
 *  [模式 1] RAIN_SENSOR_POLARITY = 1 (出厂默认, 你的当前接法)
 *     3V3 --- [覆铜板叉指] --- PA1
 *     下雨: 水膜导通 -> PA1 被 3V3 拉高 (ADC mV 大)
 *     无雨: 电极断开 -> PA1 浮空 (需用 "先放电再采样" 抑制误报)
 *     -> 判定: mV >= RAIN_SENSOR_RAIN_THRESHOLD  且 稳定多次  => 下雨
 *
 *  [模式 2] RAIN_SENSOR_POLARITY = 0 (跳线改 GND)
 *     GND --- [覆铜板叉指] --- PA1    (外部最好加一个 1M 上拉到 3V3)
 *     下雨: 水膜导通 -> PA1 被拉到 GND (ADC mV 小)
 *     无雨: 电极断开 -> 上拉到 3V3
 *     -> 判定: mV <  RAIN_SENSOR_RAIN_THRESHOLD  且 稳定多次  => 下雨
 * ==================================================================
 */
#define RAIN_SENSOR_POLARITY        1    /* 1=高有效(当前板); 0=低有效(GND跳线) */

/* 单次判雨阈值 (mV). 模式1: 大于等于此值认为这一次有水; 模式2: 小于此值认为有水 */
#define RAIN_SENSOR_RAIN_THRESHOLD  1500

/* 连续采样次数: 必须有 >= RAIN_SENSOR_HIT_COUNT 次命中阈值才认定"下雨" */
#define RAIN_SENSOR_SAMPLES         10
#define RAIN_SENSOR_HIT_COUNT       7    /* 7/10 命中就判定有效, 避免偶发噪声 */

/* 采样前先强制下拉/上拉放电的时长 (ms). 不要太长, 避免拖慢主循环 */
#define RAIN_SENSOR_PRE_DRAIN_MS    2

/* ---- API ---- */
void     Rain_Sensor_Init(void);
uint16_t Rain_Sensor_Read(void);       /* 返回 "典型 mV" (多采样后: 模式1取最高, 模式2取最低) */
bool     Rain_Sensor_IsRain(void);     /* 稳定判定后的最终结果 */

/* 调试辅助: 获取上一轮 命中次数/10, 便于串口观察抗抖效果 */
uint8_t Rain_Sensor_LastHits(void);

#endif