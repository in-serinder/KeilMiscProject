#include "bmp280_s.h"
#include "Delay.h"

#define BMP280_SENSOR_SDA GPIO_Pin_11
#define BMP280_SENSOR_SCL GPIO_Pin_10
#define BMP280_SENSOR_PORT GPIOB

#define BMP280_ADDR_WRITE  0xEC
#define BMP280_ADDR_READ   0xED

#define BMP280_REG_CALIB   0x88
#define BMP280_REG_ID      0xD0
#define BMP280_REG_CTRL    0xF4
#define BMP280_REG_CONFIG  0xF5
#define BMP280_REG_DATA    0xF7

static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static int32_t  t_fine;
static int32_t  adc_T;
static int32_t  adc_P;

#define SDA_H()   GPIO_SetBits(BMP280_SENSOR_PORT, BMP280_SENSOR_SDA)
#define SDA_L()   GPIO_ResetBits(BMP280_SENSOR_PORT, BMP280_SENSOR_SDA)
#define SCL_H()   GPIO_SetBits(BMP280_SENSOR_PORT, BMP280_SENSOR_SCL)
#define SCL_L()   GPIO_ResetBits(BMP280_SENSOR_PORT, BMP280_SENSOR_SCL)
#define SDA_READ() GPIO_ReadInputDataBit(BMP280_SENSOR_PORT, BMP280_SENSOR_SDA)

static void I2C_SDA_Out(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = BMP280_SENSOR_SDA;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BMP280_SENSOR_PORT, &g);
}

static void I2C_SDA_In(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = BMP280_SENSOR_SDA;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BMP280_SENSOR_PORT, &g);
}

static void I2C_Start(void)
{
    I2C_SDA_Out();
    SDA_H(); SCL_H(); Delay_us(5);
    SDA_L(); Delay_us(5);
    SCL_L();
}

static void I2C_Stop(void)
{
    I2C_SDA_Out();
    SCL_L(); SDA_L(); Delay_us(5);
    SCL_H(); Delay_us(5);
    SDA_H(); Delay_us(5);
}

static void I2C_SendByte(uint8_t b)
{
    uint8_t i;
    I2C_SDA_Out();
    for (i = 0; i < 8; i++)
    {
        if (b & 0x80) SDA_H(); else SDA_L();
        b <<= 1;
        Delay_us(3);
        SCL_H(); Delay_us(5);
        SCL_L(); Delay_us(3);
    }
}

static uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t i, b = 0;
    I2C_SDA_In();
    for (i = 0; i < 8; i++)
    {
        SCL_L(); Delay_us(3);
        SCL_H(); Delay_us(5);
        b <<= 1;
        if (SDA_READ()) b |= 0x01;
    }
    //发送ACK/NACK
    I2C_SDA_Out();
    if (ack) SDA_L(); else SDA_H();
    SCL_H(); Delay_us(5);
    SCL_L();
    return b;
}

static void BMP280_WriteReg(uint8_t reg, uint8_t val)
{
    I2C_Start();
    I2C_SendByte(BMP280_ADDR_WRITE);
    I2C_ReadByte(1); //读ACK，忽略返回
    I2C_SendByte(reg);
    I2C_ReadByte(1);
    I2C_SendByte(val);
    I2C_ReadByte(1);
    I2C_Stop();
}

static void BMP280_ReadBuf(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    I2C_Start();
    I2C_SendByte(BMP280_ADDR_WRITE);
    I2C_ReadByte(1);
    I2C_SendByte(reg);
    I2C_ReadByte(1);
    I2C_Start(); //重复起始
    I2C_SendByte(BMP280_ADDR_READ);
    I2C_ReadByte(1);
    for (i = 0; i < len; i++)
        buf[i] = I2C_ReadByte((i == (len - 1)) ? 0 : 1);
    I2C_Stop();
}

static void BMP280_ReadCalib(void)
{
    uint8_t buf[24];
    BMP280_ReadBuf(BMP280_REG_CALIB, buf, 24);
    dig_T1 = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    dig_T2 = (int16_t)buf[2]  | ((int16_t)buf[3] << 8);
    dig_T3 = (int16_t)buf[4]  | ((int16_t)buf[5] << 8);
    dig_P1 = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
    dig_P2 = (int16_t)buf[8]  | ((int16_t)buf[9] << 8);
    dig_P3 = (int16_t)buf[10] | ((int16_t)buf[11] << 8);
    dig_P4 = (int16_t)buf[12] | ((int16_t)buf[13] << 8);
    dig_P5 = (int16_t)buf[14] | ((int16_t)buf[15] << 8);
    dig_P6 = (int16_t)buf[16] | ((int16_t)buf[17] << 8);
    dig_P7 = (int16_t)buf[18] | ((int16_t)buf[19] << 8);
    dig_P8 = (int16_t)buf[20] | ((int16_t)buf[21] << 8);
    dig_P9 = (int16_t)buf[22] | ((int16_t)buf[23] << 8);
}

void BMP280_Sensor_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    g.GPIO_Pin = BMP280_SENSOR_SCL | BMP280_SENSOR_SDA;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BMP280_SENSOR_PORT, &g);
    SCL_H(); SDA_H();
    Delay_ms(20);

    //读取校准参数
    BMP280_ReadCalib();
    //配置：温度x1采样 压力x1采样 正常模式(0x27=001 001 11)
    BMP280_WriteReg(BMP280_REG_CTRL, 0x27);
    //配置：待机0.5ms IIR滤波x4 0x08=000 010 00
    BMP280_WriteReg(BMP280_REG_CONFIG, 0x08);
}

void BMP280_Sensor_Read(void)
{
    uint8_t buf[6];
    BMP280_ReadBuf(BMP280_REG_DATA, buf, 6);
    //3字节压力 MSB LSB XLSB -> 20bit
    adc_P = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | ((int32_t)buf[2] >> 4);
    //3字节温度
    adc_T = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | ((int32_t)buf[5] >> 4);
}

float BMP280_Sensor_ReadTemperature(void)
{
    //BME280官方补偿算法：BME280_COMPUTE_TEMP_INT
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8; //单位 0.01°C
    return (float)T / 100.0f;
}

float BMP280_Sensor_ReadPressure(void)
{
    //BME280官方补偿算法：BME280_COMPUTE_PRESS_INT (64位版本避免溢出)
    int64_t var1, var2, p;
    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
    if (var1 == 0) return 0.0f;
    p = 1048576 - (int64_t)adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4); //单位 0.25 Pa
    return (float)p / 25600.0f; //转换成 hPa(mbar)
}