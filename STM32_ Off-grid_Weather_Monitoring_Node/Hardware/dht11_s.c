#include "dht11_s.h"
#include "Delay.h"
#include "stm32f10x.h"

// 输出控制宏：1为高电平，0为低电平
#define DHT(x) ((x) ? GPIO_SetBits(GPIOB, GPIO_Pin_10) : GPIO_ResetBits(GPIOB, GPIO_Pin_10))

// PB10 引脚连接DHT11数据线

/**
  * 函    数：初始化DHT11
  * 说    明：初始化GPIO时钟和引脚，复位DHT11并检测响应
  * 返 回 值：0-成功，1-失败
  */
uint8_t init_DHT11_S(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    // 开启GPIOB时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    // 初始化为输出模式
    DHT11_MODE_OUT();
    DHT(1);  // 初始高电平

    // 复位并检测
    rstDHT11_S();
    return checkDHT11_S();
}

/**
  * 函    数：复位DHT11（发送开始信号）
  * 说    明：主机拉低至少18ms，然后拉高20-40us，等待DHT11响应
  */
void rstDHT11_S(void)
{
    DHT11_MODE_OUT();   // 切换为输出模式
    DHT(0);             // 拉低至少18ms（开始信号）
    Delay_ms(20);
    DHT(1);             // 拉高20-40us（结束开始信号）
    Delay_us(30);
}

/**
  * 函    数：检测DHT11响应
  * 说    明：DHT11会先拉低80us再拉高80us作为响应信号
  * 返 回 值：0-成功，1-失败
  */
uint8_t checkDHT11_S(void)
{
    uint8_t retry = 0;
    
    DHT11_MODE_IN();    // 切换为输入模式，等待DHT11响应
    
    // 等待DHT11拉低（响应信号）
    while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) && retry < 100) {
        retry++;
        Delay_us(1);
    }
    if (retry >= 100) return 1; // 超时
    
    retry = 0;
    // 等待DHT11拉高（准备发送数据）
    while (!GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) && retry < 100) {
        retry++;
        Delay_us(1);
    }
    if (retry >= 100) return 1; // 超时
    
    return 0; // 成功
}

/**
  * 函    数：读取一个数据位
  * 说    明：根据高电平持续时间判断0或1（26-28us为0，70us为1）
  * 返 回 值：0或1
  */
uint8_t readDHT11_S(void)
{
    uint8_t retry = 0;
    
    // 等待DHT11拉低（开始位）
    while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) && retry < 100) {
        retry++;
        Delay_us(1);
    }
    // 等待DHT11拉高
    retry = 0;
    while (!GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) && retry < 100) {
        retry++;
        Delay_us(1);
    }
    
    Delay_us(40); // 延时40us后判断电平
    
    // 高电平持续40us后如果还是高电平，说明是1（70us）
    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10)) 
        return 1;  // 数据位1
    else
        return 0;  // 数据位0
}

/**
  * 函    数：读取一个字节
  * 说    明：连续读取8个数据位组成一个字节
  * 返 回 值：读取到的字节数据
  */
uint8_t readDHT11_S_Byte(void)
{
    uint8_t i, dat = 0;
    for (i = 0; i < 8; i++) {
        dat <<= 1;           // 左移一位
        dat |= readDHT11_S(); // 读取当前位并加到最低位
    }
    return dat;
}

/**
  * 函    数：读取温湿度数据
  * 说    明：读取5字节数据（湿度整数/小数，温度整数/小数，校验和）
  * 参    数：temperature - 温度指针，humidity - 湿度指针
  * 返 回 值：0-成功 1-检测失败 2-校验失败
  */
uint8_t DHT11_S_Read(uint8_t *temperature, uint8_t *humidity)
{
    uint8_t data[5] = {0};
    uint8_t i;
    
    rstDHT11_S();  // 复位DHT11
    
    if (checkDHT11_S() != 0) return 1; // 检测失败
    
    // 读取5个字节数据
    for (i = 0; i < 5; i++) {
        data[i] = readDHT11_S_Byte();
    }
    
    // 校验和验证：前4字节之和等于第5字节
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        return 2; // 校验失败
    }
    
    *humidity = data[0];    // 湿度整数部分
    *temperature = data[2]; // 温度整数部分
    
    return 0; // 成功
}