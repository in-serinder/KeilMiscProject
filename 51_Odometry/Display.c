#include "Display.h"

/* 根据里程值大小自动选择右半4位显示格式（可显尽显：优先保留小数位）
 * 4位宽度限制：xxx.x 最多只能到 999.9，再大就放不下小数点了
 *   mil < 9.995 -> 2位小数  x.xx    (9.995 起四舍五入为 10.0，4位仍放得下)
 *   mil < 999.9 -> 1位小数  xxx.x   (三位整数也保留小数点，不再丢小数)
 *   否则        -> 0位小数  xxxx    (四位整数时才挤掉小数点)          */
static uint8_t AutoMileFormat(float mil)
{
    if (mil < 9.995f)  return 2;   /*   x.xx */
    if (mil < 999.9f)  return 1;   /* xxx.x  */
    return 0;                      /*  xxxx  */
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


/* 重置成功：显示 SUCCEED + 停顿 1.5s 虽然当前还是不对 */
void Display_ShowSuccess(void)
{
    uint8_t code succeed_segs[] = { SEG_S, SEG_U, SEG_C, SEG_C,
                                    SEG_E, SEG_E, SEG_E, SEG_D };
    Max7219_DisplayCustomSegments(succeed_segs, 8);
    Delay_ms(1500);
    Max7219_Clear();
}


void Display_SM_Mode(float speed, float mil){
    uint8_t spd_fmt, mil_fmt;

    /* 右半区只有4位，超过 9999 无法显示，饱和处理避免取整溢出后归零 */
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