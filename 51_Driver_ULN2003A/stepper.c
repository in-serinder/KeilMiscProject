#include "stepper.h"

/* ==========================================================================
 * 步进电机驱动实现（ULN2003A 灌电流，4 相单极性）
 * --------------------------------------------------------------------------
 * 相序表用 7 位掩码表示，bit0=IN1 … bit6=IN7，直接对应 P2.0~P2.6。
 * 常用三种通电方式：
 *   WAVE  单四拍  A   B   C   D          -> 01 02 04 08
 *   FULL  双四拍  AB  BC  CD  DA         -> 03 06 0C 09
 *   HALF  八拍    A AB B BC C CD D DA    -> 01 03 02 06 04 0C 08 09
 *
 * 注意：
 *   1. 换相时先断电再通电会造成短暂失电，力矩略降，但绝不烧管；
 *      ULN2003A 没有续流问题（内部带钳位二极管），可以放心高速切换。
 *   2. 停止时一定要调 Stepper_AllOff()，否则绕组长期通电会发烫。
 *   3. P2 整口用影子寄存器写，只动 bit0~bit6，保留 bit7 不受影响。
 * ========================================================================== */

static unsigned char s_port;   /* P2 影子，只维护低 7 位 */
static unsigned char s_mode;
static unsigned char s_speed;
static unsigned char s_idx;    /* 当前在相序表里的下标 */
static unsigned int  s_pos;    /* 累计拍数（正转加、反转减） */

static unsigned char code seq_wave[4] = {0x01, 0x02, 0x04, 0x08};
static unsigned char code seq_full[4] = {0x03, 0x06, 0x0C, 0x09};
static unsigned char code seq_half[8] = {0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09};

/* 速度 1~10 对应的延时循环次数（越大越慢）。
   每轮约 4 个机器周期 ≈ 4.3us，所以 1500 轮 ≈ 6.5ms/拍，140 轮 ≈ 0.6ms/拍。 */
static unsigned int code delay_tab[10] = {1500, 1200, 950, 750, 580, 440, 330, 240, 180, 140};

static void stepper_delay(void)
{
    unsigned int i;
    unsigned int t;

    t = delay_tab[s_speed - 1];
    for (i = 0; i < t; i++)
        _nop_();
}

/* 写 7 路掩码：bit0=IN1 … bit6=IN7 */
void Stepper_Output(unsigned char in_mask)
{
    s_port = (unsigned char)((s_port & 0x80) | (in_mask & STEPPER_MASK));
    P2 = s_port;
}

void Stepper_AllOff(void)
{
    Stepper_Output(0x00);
}

void Stepper_Init(void)
{
    s_port = (unsigned char)(P2 | 0x80); /* 读回初值，bit7 保持为 1 */
    s_mode = STEPPER_FULL;
    s_speed = 5;
    s_idx = 0;
    s_pos = 0;
    Stepper_AllOff();
}

void Stepper_SetMode(unsigned char mode)
{
    if (mode > STEPPER_HALF)
        mode = STEPPER_FULL;
    s_mode = mode;
    s_idx = 0; /* 换方式后从相序表起点重新开始，避免错拍 */
}

unsigned char Stepper_Mode(void)
{
    return s_mode;
}

void Stepper_SetSpeed(unsigned char speed)
{
    if (speed < STEPPER_SPEED_MIN)
        speed = STEPPER_SPEED_MIN;
    if (speed > STEPPER_SPEED_MAX)
        speed = STEPPER_SPEED_MAX;
    s_speed = speed;
}

unsigned char Stepper_Speed(void)
{
    return s_speed;
}

void Stepper_Step(bit dir)
{
    unsigned char n, mask;

    n = (s_mode == STEPPER_HALF) ? 8 : 4;

    if (dir)
    {
        s_idx++;
        if (s_idx >= n)
            s_idx = 0;
        s_pos++;
    }
    else
    {
        if (s_idx == 0)
            s_idx = n;
        s_idx--;
        s_pos--;
    }

    if (s_mode == STEPPER_WAVE)
        mask = seq_wave[s_idx];
    else if (s_mode == STEPPER_FULL)
        mask = seq_full[s_idx];
    else
        mask = seq_half[s_idx];

    Stepper_Output(mask);
    stepper_delay();
}

void Stepper_Steps(unsigned int n, bit dir)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        Stepper_Step(dir);
}

void Stepper_Turns(unsigned char n, bit dir)
{
    unsigned int steps;

    steps = (unsigned int)STEPPER_STEPS_PER_REV * n;
    if (s_mode == STEPPER_HALF)
        steps <<= 1; /* 八拍时步距减半，一圈要两倍拍数 */
    Stepper_Steps(steps, dir);
}

unsigned int Stepper_Position(void)
{
    return s_pos;
}
