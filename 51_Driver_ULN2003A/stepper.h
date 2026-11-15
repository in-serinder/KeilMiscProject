#ifndef __STEPPER_H__
#define __STEPPER_H__

#include "stc89c52.c"

/* ==========================================================================
 * 步进电机通用驱动（HY25E6S + ULN2003A）
 * --------------------------------------------------------------------------
 * 接线：P20 ~ P26 -> ULN2003A 的 IN1 ~ IN7
 *
 * ULN2003A 是 7 路达林顿**灌电流**驱动：
 *   输入高 -> 对应输出管导通 -> 该相绕组对地导通（绕组另一端接 +V）
 * 所以"写 1 = 这一相得电"，不需要反相。
 *
 * 说明：ULN2003A 有 7 路通道，但 4 相单极性步进只用前 4 路；
 *       IN5~IN7 保持低电平。如果电机是 5 相或用了别的通道，
 *       只需改 stepper.c 里的相序表（掩码 bit0~bit6 直接对应 IN1~IN7）。
 *
 * 电机公共端（一般 5 根线里的红线）接 +V，电源地和单片机地必须共地。
 * ========================================================================== */

/* ---------------- 7 路输入引脚 ---------------- */
#define STEPPER_IN1     P20
#define STEPPER_IN2     P21
#define STEPPER_IN3     P22
#define STEPPER_IN4     P23
#define STEPPER_IN5     P24
#define STEPPER_IN6     P25
#define STEPPER_IN7     P26

/* P20~P26 正好是 P2 的 bit0~bit6，可以用一个 7 位掩码直接写口 */
#define STEPPER_MASK    0x7F

/* ---------------- 驱动方式 ---------------- */
#define STEPPER_WAVE    0   /* 单四拍：A-B-C-D，力矩最小、最省电 */
#define STEPPER_FULL    1   /* 双四拍：AB-BC-CD-DA，力矩最大 */
#define STEPPER_HALF    2   /* 八拍：A-AB-B-BC-C-CD-D-DA，步距减半、最平滑 */

/* ---------------- 速度 ---------------- */
#define STEPPER_SPEED_MIN   1
#define STEPPER_SPEED_MAX   10

/* 整步转一圈的拍数：按电机步距角改。
   1.8°/步（常见两相混合式）-> 200；0.9° -> 400；减速电机按输出轴折算。 */
#define STEPPER_STEPS_PER_REV   200

/* ---------------- API ---------------- */
void Stepper_Init(void);

void Stepper_SetMode(unsigned char mode);
unsigned char Stepper_Mode(void);

void Stepper_SetSpeed(unsigned char speed);     /* 1 最慢 … 10 最快 */
unsigned char Stepper_Speed(void);

void Stepper_Output(unsigned char in_mask);     /* 直接写 7 路掩码，调试用 */
void Stepper_AllOff(void);                      /* 全部断电，停止时防发热 */

void Stepper_Step(bit dir);                     /* 走一拍：1=正转 0=反转 */
void Stepper_Steps(unsigned int n, bit dir);
void Stepper_Turns(unsigned char n, bit dir);   /* 按整圈走 */

unsigned int Stepper_Position(void);            /* 累计拍数 */

/* ---------------- 测试（实现在 stepper_test.c） ---------------- */
void Stepper_TestAll(void);

#endif /* __STEPPER_H__ */
