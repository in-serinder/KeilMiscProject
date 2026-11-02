/* board.c — 端口工作模式与上电默认状态 */
#include "board.h"
#include "delay.h"

void Board_Init(void)
{
    /* P3.2 / P3.3：I2C 的 SCL/SDA，保持准双向（内部弱上拉，配合外部 4.7k） */
    P3M1 &= (uint8_t)~0x0C;
    P3M0 &= (uint8_t)~0x0C;

    /* P3.0 / P3.1：UART1 RXD/TXD，准双向 */
    P3M1 &= (uint8_t)~0x03;
    P3M0 &= (uint8_t)~0x03;

    /* P5.4 / P5.5：TM1637 CLK/DIO，推挽输出（无需读回，边沿更陡）
     * 注意：STC8G1K 的 P5.4 兼作复位脚，必须在 STC-ISP 里勾选
     *       "复位脚用作 I/O 口"，否则这脚永远输出不了，数码管会全黑。 */
    P5M1 &= (uint8_t)~0x30;
    P5M0 |= 0x30;
    TM1637_CLK = 1;
    TM1637_DIO = 1;

#if CFG_EEPROM_WP_ENABLE
    /* P3.4 / P3.5：两路 EEPROM 写保护，推挽输出 */
    P3M1 &= (uint8_t)~0x30;
    P3M0 |= 0x30;
    EE_WP_CH0 = 0;          /* 低电平 = 解除写保护 */
    EE_WP_CH1 = 0;
#endif
}

bit Board_TM1637PinsOk(void)
{
    bit ok;

    P5M1 &= (uint8_t)~0x30;
    P5M0 |= 0x30;

    TM1637_CLK = 0;
    TM1637_DIO = 0;
    delay_us(2);

    /* 输出 0 之后读回应当还是 0；若读回 1，说明这脚没在 CPU 手上
     * （最典型：P5.4 仍是 RST，或者外部有更强的上拉/接到了别处）。 */
    ok = (bit)((TM1637_CLK == 0) && (TM1637_DIO == 0));

    TM1637_CLK = 1;
    TM1637_DIO = 1;
    delay_us(2);

    return ok;
}
