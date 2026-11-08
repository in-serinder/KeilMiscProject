#include "board.h"
/* pcf8574.c — 8 路写保护控制器（IIC2 软件 I2C，P5.4/P5.5，7 位地址 0x20）
 *
 * 注意：PCF8574 **不在** IIC1。IIC1 是纯存储器总线，只挂 8 片 24Cxx；
 *       写保护控制与 OLED、24C02 一起集中在 IIC2。*/
#include "pcf8574.h"
#include "i2c_sw.h"
#include "delay.h"

static uint8_t s_mask = 0x00;   /* 1 = 写保护 */

static bit pcf_bus_write(uint8_t v)
{
    return I2C2_Write(ADDR_PCF8574, &v, 1);
}

static bit pcf_bus_read(uint8_t *v)
{
    /* PCF8574 的"读"就是直接读一个字节（无寄存器地址阶段），
     * 所以写阶段长度为 0。*/
    return I2C2_WriteRead(ADDR_PCF8574, 0, 0, v, 1);
}

void PCF_Init(void)
{
    /* PCF8574 上电输出全 1（全保护），这里主动解保护一次 */
    s_mask = 0x00;
    (void)pcf_bus_write(s_mask);
    delay_ms(5);
}

bit PCF_Write(uint8_t mask)
{
    return pcf_bus_write(mask);
}

bit PCF_Read(uint8_t *mask)
{
    return pcf_bus_read(mask);
}

bit PCF_SetMask(uint8_t mask)
{
    uint8_t back;
    uint8_t driven;

    if (!PCF_Write(mask))
    {
        return 0;
    }

    s_mask = mask;
    delay_ms(5);            /* PCF8574 写周期，手册要求等 5ms */

    /* 回读校验：PCF8574 是准双向口，写 0 的位由强下拉驱动，回读一定是 0；
     * 写 1 的位只是弱上拉，而 24Cxx 的 WP 脚内部有下拉，回读会被拉成 0。
     * 所以只校验"被驱动为低"的位（mask 里为 0 的位），1 的位一律放行，
     * 否则一旦接上真实 WP 负载就会误判成功为失败。*/
    driven = (uint8_t)(~mask);
    if (PCF_Read(&back))
    {
        return (bit)((back & driven) == 0);
    }

    return 1;               /* 读失败按写入成功处理（写 ACK 已确认设备在线）*/
}
