/* =====================================================================
 * main.c — 24Cxx 系列 EEPROM 多路烧录器 · 下位机固件
 *
 * 单片机：STC8G1K08A（1T 内核，22.1184MHz，SOP8；8K Flash / 256B idata / 1K xdata）
 * 串口  ：115200 8N1，帧格式 >内容<\r\n
 * 硬件  ：P3.2/P3.3 硬件 I2C 挂 24Cxx（I2C_S 必须为 00，见 board.h）；P5.4/P5.5 接 TM1637
 *
 * 上电流程：
 *   Board_Init  -> 端口模式、EEPROM 写保护放开
 *   Uart1_Init  -> 115200，开接收中断
 *   I2C_Init    -> 主机模式，约 251kHz
 *   Copier_Init -> 状态机复位 + 数码管显示 IDLE
 *   EA = 1      -> 开总中断，进入命令循环
 * ===================================================================== */
#include "board.h"
#include "delay.h"
#include "uart.h"
#include "proto.h"
#include "copier.h"
#include "display.h"
#include "HW_24c64.h"

void main(void)
{
    EA = 0;                 /* 初始化期间关总中断，避免半初始化状态被打断 */

    Board_Init();
    Uart1_Init();
    I2C_Init();
    delay_ms(20);           /* 等 24Cxx 上电稳定 */

    Copier_Init();

    EA = 1;                 /* 允许中断：串口收字节 -> Proto_OnByte */

    for (;;)
    {
        Copier_Task();      /* 取帧 -> 解析 -> 执行；无帧立即返回 */
    }
}
