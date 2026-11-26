#include "Display.h"

/* 根据里程值大小自动选择右半4位显示格式
 * 右半区只有 4 位，所以：
 *   9.99    km 以内 → 2 位小数
 *   99.9 / 123.4 km → 1 位小数（三位数也保留小数点）
 *   999 / 9999  km  → 不带小数
 * 阈值取"四舍五入前的边界"，避免 9.996 被显示成 10.0 时多占一位。 */
static uint8_t AutoMileFormat(float mil)
{
    if (mil < 9.995f)   return 2;   /*   9.99 km */
    if (mil < 999.95f)  return 1;   /*  99.9 / 999.9 km */
    return 0;                       /* 1000 ~ 9999 km */
}


void Display_Init(void)
{
    Max7219_Init();
}


/* 重置确认动画：全显 8. 闪烁3次 */
void Display_ResetAnimation(void)
{
    uint8_t bl, di;
    for (bl = 0; bl < 3; bl++) {
        for (di = 1; di <= 8; di++) Max7219_Display(di, 8, 1);
        Delay_ms(120);
        Max7219_Clear();
        Delay_ms(80);
    }
}


void Display_SM_Mode(float speed, float mil){
    uint8_t spd_fmt, mil_fmt;

    /* 右半区只有 4 位，超过 9999 就饱和，避免取整溢出后归零 */
    if (mil > 9999.4f) mil = 9999.4f;

    spd_fmt = (speed < 0.5f) ? 3 : 2;   /* 低速时用3位小数防闪烁 */
    mil_fmt = AutoMileFormat(mil);

    LED_SM_Mode();
    Max7219_DisplayDualFmt(speed, spd_fmt, mil, mil_fmt);
}


void Display_Obometry_Mode(float mil){
    LED_Obometry_Mode();
    Max7219_DisplayFloat(mil, 3);
}