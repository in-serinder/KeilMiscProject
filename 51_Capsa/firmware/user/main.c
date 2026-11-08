/* main.c — Capsa 存储卡固件主程序（STC8G1K17A / 22.1184MHz）*/
#include "board.h"
#include "delay.h"
#include "uart.h"
#include "proto.h"
#include "app.h"
#include "ee.h"
#include "meta.h"
#include "pcf8574.h"
#include "ui.h"

static char xdata s_frame[CFG_FRAME_MAX];

void main(void)
{
    uint8_t r;
    bit pins_ok;

    EA = 0;                         /* 初始化期间关总中断，避免半初始化状态被打断 */

    Board_Init();

    /* 串口必须在 I2C 之前：上电后上位机可能 100ms 内就开始发 TYPE-CAPASA */
    Uart1_Init();

    /* ★ 总中断必须打开，串口接收靠它（Uart1_Isr -> Proto_OnByte）。
     *   漏掉 EA 的现象就是"发任何命令都没有返回值"——RI 置位后没人取走，
     *   下一字节把 SBUF 覆盖，帧永远组不起来。
     *   这里而不是循环前打开：后面的 24C02 读取、OLED 初始化要几十毫秒，
     *   尽早开中断才不会漏掉上位机的握手帧（中断里只写协议缓冲，安全）。*/
    EA = 1;

    EE_InitArray();                 /* IIC1 硬件 I2C（只挂 8 片 24Cxx）+ 默认 24C64 */
    M24_Init();                     /* IIC2：初始化并**选定引脚组**（P5.4/P5.5 或 P1.4/P1.5）*/

    /* 引脚自检必须在 OLED 初始化之前做：它会推挽拉低再恢复，
     * 在 I2C 空闲时等价于一个假 START，会把已初始化的屏打进"等时钟"状态。*/
    pins_ok = Board_SwI2cPinsOk();

#if CFG_DISPLAY_ENABLE
    UI_Init();
#endif

    /* ---- 元数据 ----
     * ★ 三条同时成立才算"卡上有可用的属性"：检测区被写过、字段读得出来、
     *   字段自洽（Meta_InfoValid）。只满足前两条时（半初始化 / 擦到一半），
     *   型号字段可能是 0xFFFFFFFF，EE_SetModel 会跟着变成非法值，
     *   所以这里宁可退回默认，让屏和上位机都走"未初始化"流程。*/
    if (Meta_IsInitialized() && Meta_Load() && Meta_InfoValid())
    {
        EE_SetModel((uint16_t)g_meta.model_kbit);
    }
    else
    {
        g_meta.init_flag = 0;
        g_meta.wp_mask = 0x00;
        g_meta.health_mask = 0xFF;
        Meta_RecalcCapacity();   /* wp_mask = 0 -> 8 片口径；口径统一从这一处出 */
        g_meta.model_kbit = EE_MODEL_24C64;
        g_meta.used = 0;
        g_meta.medium = 0;
    }

    /* WP 对齐：PCF8574 上电输出全 1（全是保护态），必须先解保护，
     * 再按 24C02 里的掩码重发一次，否则第一包 W_ 就会吃 [ERR_WP]。
     * PCF8574 挂在 IIC2，此时 IIC2 已由 M24_Init() 初始化完毕。*/
    PCF_Init();
    if (g_meta.init_flag)
    {
        (void)PCF_SetMask(g_meta.wp_mask);
    }

    App_Init();

#if CFG_DISPLAY_ENABLE
    {
        uint8_t scan = EE_ScanMask();
        UI_Boot(pins_ok, scan);
    }
#endif

    /* 总中断已在 Uart1_Init() 之后打开，此处直接进命令循环 */
    for (;;)
    {
        r = Proto_Poll(s_frame);
        if (r == PROTO_OK)
        {
            App_Handle(s_frame);
        }
        else if (r == PROTO_BAD)
        {
            Proto_SendAck("RX-ERR");
        }
        else
        {
            App_Tick();
        }
    }
}
