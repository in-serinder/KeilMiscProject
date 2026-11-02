/* =====================================================================
 * display.c — 状态显示（2 位状态码 + 2 位十六进制地址）
 *
 * 七段管能看清的字母有限，状态码只用这些：
 *   0-9 A b C d E F H I L n o P S t U y - （空格）
 * 特别说明：
 *   'W' 没有对应字形，只能用 0x3E 画成 "U"，所以写流程统一用 U；
 *   'R' 只能用 e+g 两段（0x50），看着像杂点，所以状态码里不用 R。
 *
 *   IDLE            未选器件，显示 "IdLE"
 *   Idxx            空闲但已选过器件
 *   SExx            已设 ROUTER（Select），等 EDGE
 *   Edxx            已设 EDGE（Edge），等 INT_R/INT_W
 *   UCxx            写流程进行中（U = write，C = 忙）
 *   dCxx            读流程进行中（d = Download，C = 忙）
 *
 * ★ 为什么"等待/当前包/完成"三种子状态共用一个码：
 *   它们每个只持续几毫秒（一包 16 字节 ≈ 10ms），切换频率 ~90~180Hz，
 *   人眼会把几帧叠加起来看。例如 C(0x39=a,d,e,f) ∪ o(0x5C=c,d,e,g)
 *   = a,c,d,e,f,g = 0x7D，正好是数字 "6" —— 于是 "UC50" 看起来像 "U650"。
 *   所以流程内只保留一个不闪的码，只在流程开始/结束时切换。
 * ===================================================================== */
#include "display.h"
#include "copier.h"
#include "TM1637.h"
#include "delay.h"

#if CFG_DISPLAY_ENABLE

static const char code s_pfx[9][2] =
{
    { 'I', 'd' },   /* ST_IDLE       空闲          */
    { 'S', 'E' },   /* ST_ROUTER_SET 已选器件      */
    { 'E', 'd' },   /* ST_EDGE_SET   已设 EDGE     */
    { 'U', 'C' },   /* ST_WRITE_WAIT 写流程进行中  */
    { 'U', 'C' },   /* ST_WRITE_CURR 写流程进行中  */
    { 'U', 'C' },   /* ST_WRITE_OVER 写流程进行中  */
    { 'd', 'C' },   /* ST_READ_WAIT  读流程进行中  */
    { 'd', 'C' },   /* ST_READ_CURR  读流程进行中  */
    { 'd', 'C' }    /* ST_READ_OVER  读流程进行中  */
};

static char xdata s_last[4];
static bit s_valid = 0;

static char hex_char(uint8_t v)
{
    v &= 0x0F;
    return (v < 10) ? (char)('0' + v) : (char)('A' + v - 10);
}

void Disp_Init(void)
{
    s_last[0] = ' ';
    s_last[1] = ' ';
    s_last[2] = ' ';
    s_last[3] = ' ';
    s_valid = 0;

    TM1637_Init();
    Disp_Update(ST_IDLE, 0);
}

void Disp_Update(uint8_t state, uint8_t router)
{
    char xdata f[4];
    uint8_t i;

    if (state > ST_READ_OVER)
    {
        state = ST_IDLE;
    }

    if (state == ST_IDLE && router == 0)
    {
        f[0] = 'I';
        f[1] = 'd';
        f[2] = 'L';
        f[3] = 'E';
    }
    else
    {
        f[0] = s_pfx[state][0];
        f[1] = s_pfx[state][1];
        f[2] = hex_char((uint8_t)(router >> 4));
        f[3] = hex_char((uint8_t)(router & 0x0F));
    }

    if (s_valid)
    {
        for (i = 0; i < 4; i++)
        {
            if (f[i] != s_last[i])
            {
                break;
            }
        }
        if (i == 4)
        {
            return;             /* 内容没变，不刷屏（省 TM1637 总线时间） */
        }
    }

    for (i = 0; i < 4; i++)
    {
        s_last[i] = f[i];
    }
    s_valid = 1;

    TM1637_Show4(f, 0);
}

bit Disp_SelfTest(void)
{
    const uint8_t all[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    bit pins_ok;
    uint8_t i;

    pins_ok = Board_TM1637PinsOk();

    TM1637_SetBright(7);
    TM1637_WriteSegs(all, 4);           /* 全段点亮（应看到 8.8.8.8） */

    for (i = 0; i < 20; i++)            /* 约 200ms */
    {
        delay_ms(10);
    }

    TM1637_Clear();
    s_valid = 0;                        /* 让下一次 Disp_Update 强制重画 */

    return pins_ok;
}

#else   /* CFG_DISPLAY_ENABLE == 0 */

void Disp_Init(void) {}
void Disp_Update(uint8_t state, uint8_t router) { state = state; router = router; }
bit Disp_SelfTest(void) { return 0; }

#endif
