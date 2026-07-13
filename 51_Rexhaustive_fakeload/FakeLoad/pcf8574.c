#include "pcf8574.h"

// 保存当前端口状态
uint8_t xdata pcf8574_port_data = 0xFF;
static uint8_t idata i;
// 延时函数（使用局部变量，避免与I2C_SendByte的循环计数器冲突）
void I2C_Delay(void) {
  unsigned char idata dly = 100;
  while (dly--)
    ;
}

// 初始化I2C总线
void I2C_Init(void) {
  SDA = 1;
  SCL = 1;
}

// 发送起始信号
void I2C_Start(void) {
  SDA = 1;
  SCL = 1;
  I2C_Delay();
  SDA = 0;
  I2C_Delay();
  SCL = 0;
}

// 发送停止信号
void I2C_Stop(void) {
  SDA = 0;
  SCL = 1;
  I2C_Delay();
  SDA = 1;
  I2C_Delay();
}

// 发送一个字节
void I2C_SendByte(unsigned char byte) {
  for (i = 0; i < 8; i++) {
    SDA = byte & 0x80;
    byte <<= 1;
    SCL = 1;
    I2C_Delay();
    SCL = 0;
    I2C_Delay();
  }
}

// 接收一个字节
unsigned char I2C_ReceiveByte(void) {
  unsigned char xdata byte = 0;
  SDA = 1;
  for (i = 0; i < 8; i++) {
    byte <<= 1;
    SCL = 1;
    I2C_Delay();
    byte |= SDA;
    SCL = 0;
    I2C_Delay();
  }
  return byte;
}

// 等待应答
bit I2C_WaitAck(void) {
  bit ack;
  SDA = 1;
  I2C_Delay();
  SCL = 1;
  I2C_Delay();
  ack = SDA;
  SCL = 0;
  I2C_Delay();
  return ack;
}

// 发送应答
void I2C_SendAck(bit ack) {
  SDA = ack;
  I2C_Delay();
  SCL = 1;
  I2C_Delay();
  SCL = 0;
  I2C_Delay();
}

/**
 * @brief  初始化PCF8574扩展IO芯片
 * @details 初始化I2C总线，并将PCF8574所有端口设置为高电平输出
 * @note   PCF8574上电后默认高阻态，需初始化后才能正常使用
 * @param  无
 * @return 无
 */
void PCF8574_Init(void) {
  I2C_Init();
  // 默认所有端口输出高电平
  PCF8574_Write(0xFF);
}

/**
 * @brief  读取PCF8574所有8位端口的状态
 * @details 通过I2C总线读取PCF8574的输入端口状态
 * @note   PCF8574为准双向端口，可同时作为输入和输出
 * @param  无
 * @return 返回8位端口状态，bit0~bit7对应P0~P7
 *         返回值中每一位为1表示高电平，为0表示低电平
 * @retval 读取成功返回当前端口状态
 * @retval 读取失败返回上次保存的端口状态
 */
unsigned char PCF8574_Read(void) {
  unsigned char dat;

  I2C_Start();
  I2C_SendByte(PCF8574_R_ADDR);
  if (I2C_WaitAck())
    return pcf8574_port_data; // 读取失败，返回上次保存的值

  dat = I2C_ReceiveByte();
  I2C_SendAck(1); // 发送NACK
  I2C_Stop();

  pcf8574_port_data = dat;
  return dat;
}

/**
 * @brief  写入数据到PCF8574所有8位端口
 * @details 通过I2C总线向PCF8574写入8位数据，控制所有端口输出状态
 * @note   PCF8574的输出为开漏输出，需要外接上拉电阻
 * @param  dat: 要写入的8位数据，bit0~bit7对应P0~P7
 *         每一位为1表示输出高电平(高阻态)，为0表示输出低电平
 * @return 无
 */
void PCF8574_Write(unsigned char dat) {
  I2C_Start();
  I2C_SendByte(PCF8574_W_ADDR);
  if (I2C_WaitAck()) {
    // ACK失败，可能地址错误或设备未响应
    I2C_Stop();
    return;
  }

  I2C_SendByte(dat);
  if (I2C_WaitAck()) {
    I2C_Stop();
    return;
  }

  I2C_Stop();
  pcf8574_port_data = dat;
}

/**
 * @brief  设置指定端口的输出状态
 * @details 单独控制PCF8574的某一个端口电平状态
 * @note   内部会保存当前端口状态，只修改指定端口
 * @param  port: 端口号，范围0~7，对应P0~P7
 * @param  state: 端口状态，0表示低电平，1表示高电平
 * @return 无
 * @warning port参数超出0~7范围时函数不执行任何操作
 */
void PCF8574_SetPort(unsigned char port, bit state) {
  unsigned char temp_data = pcf8574_port_data;
  if (port > 7)
    return;

  // 先保存当前值

  if (state)
    temp_data |= (1 << port);
  else
    temp_data &= ~(1 << port);

  // 只有当值改变时才发送
  if (temp_data != pcf8574_port_data) {
    PCF8574_Write(temp_data);
  }
}

/**
 * @brief  读取指定端口的状态
 * @details 从PCF8574读取指定端口的当前电平状态
 * @note   会先调用PCF8574_Read()获取最新端口状态
 * @param  port: 端口号，范围0~7，对应P0~P7
 * @return 返回指定端口的状态，0表示低电平，1表示高电平
 * @retval 0: 端口为低电平或参数错误
 * @retval 1: 端口为高电平
 * @warning port参数超出0~7范围时返回0
 */
bit PCF8574_GetPort(unsigned char port) {
  if (port > 7)
    return 0;

  return (PCF8574_Read() >> port) & 0x01;
}