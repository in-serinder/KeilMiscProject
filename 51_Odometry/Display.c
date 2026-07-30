#include "Display.h"

/* 根据里程值大小自动选择右半4位显示格式 */
static uint8_t AutoMileFormat(float mil)
{
    if (mil < 10.0f)  return 2;   /*   9.99 km */
    if (mil < 100.0f) return 1;   /*  99.9 km */
    return 0;                      /* 999 km */
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

    spd_fmt = (speed < 0.5f) ? 3 : 2;   /* 低速时用3位小数防闪烁 */
    mil_fmt = AutoMileFormat(mil);

    LED_SM_Mode();
    Max7219_DisplayDualFmt(speed, spd_fmt, mil, mil_fmt);
}


void Display_Obometry_Mode(float mil){
    LED_Obometry_Mode();
    Max7219_DisplayFloat(mil, 3);
}