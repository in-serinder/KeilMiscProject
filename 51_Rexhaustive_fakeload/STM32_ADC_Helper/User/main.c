#include "Delay.h"
#include "Serial.h"
#include "stm32f10x.h"
#include <stdio.h>


/* ========== 硬件配置 ========== */
#define ADC_GPIO_PORT GPIOA
#define ADC_GPIO_PIN GPIO_Pin_1 // PA1 ADC输入
#define ADC_CHANNEL ADC_Channel_1

/* 分压电阻：R1=30kΩ, R2=4.7kΩ */
#define DIVIDER_R1 30000.0f
#define DIVIDER_R2 4700.0f
#define DIVIDER_RATIO ((DIVIDER_R1 + DIVIDER_R2) / DIVIDER_R2) // ≈7.383

#define ADC_REF_VOLTAGE 3.3f   // STM32 VREF
#define ADC_RESOLUTION 4096.0f // 12-bit

/* ========== 命令缓冲区（中断填充，主循环消费） ========== */
#define CMD_BUF_SIZE 32
static char cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_idx = 0;
static volatile uint8_t cmd_ready = 0;

/* ========== ADC值 ========== */
static volatile uint16_t adc_raw = 0;

/* ========== 函数声明 ========== */
static void GPIO_Config(void);
static void ADC_Config(void);
static void Delay(volatile uint32_t count);
static void ProcessCommand(const char *cmd);
static uint8_t str_has(const char *s, const char *keyword);

/******************************************************************************/
int main(void) {
  GPIO_Config();
  Serial_Init();
  ADC_Config();

  ADC_SoftwareStartConvCmd(ADC1, ENABLE);
  Delay(72000);

  Serial_SendString("\r\n=== STM32 ADC Auto Report ===\r\n");

  while (1) {
    /* 读取ADC最新值 */
    if (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == SET) {
      adc_raw = ADC_GetConversionValue(ADC1);
    }

    /* 处理串口收到的AT指令 */
    if (cmd_ready) {
      cmd_ready = 0;
      USART_ITConfig(USART1, USART_IT_RXNE, DISABLE); // 处理期间关中断
      ProcessCommand(cmd_buf);
      USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    }

    /* ===== 自动上报（每1s） ===== */
    char buf[32];
    float v_adc = (float)adc_raw * ADC_REF_VOLTAGE / ADC_RESOLUTION;
    float v_in = v_adc * DIVIDER_RATIO;

    GPIO_ResetBits(GPIOC, GPIO_Pin_13); // LED亮
    sprintf(buf, ">> %.2f\r\n", v_in);
    Serial_SendString(buf);
    GPIO_SetBits(GPIOC, GPIO_Pin_13); // LED灭

    Delay_ms(1000); // 使用System/Delay.c提供的精确1秒延时
  }
}

/******************************************************************************
 * GPIO 初始化
 ******************************************************************************/
static void GPIO_Config(void) {
  GPIO_InitTypeDef gpio;
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);

  gpio.GPIO_Mode = GPIO_Mode_Out_PP;
  gpio.GPIO_Pin = GPIO_Pin_13;
  gpio.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOC, &gpio);
  GPIO_SetBits(GPIOC, GPIO_Pin_13);

  gpio.GPIO_Mode = GPIO_Mode_AIN;
  gpio.GPIO_Pin = ADC_GPIO_PIN;
  GPIO_Init(ADC_GPIO_PORT, &gpio);
}

/******************************************************************************
 * ADC1 初始化 (PA1, 连续转换)
 ******************************************************************************/
static void ADC_Config(void) {
  ADC_InitTypeDef adc;
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

  adc.ADC_Mode = ADC_Mode_Independent;
  adc.ADC_ScanConvMode = DISABLE;
  adc.ADC_ContinuousConvMode = ENABLE;
  adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
  adc.ADC_DataAlign = ADC_DataAlign_Right;
  adc.ADC_NbrOfChannel = 1;
  ADC_Init(ADC1, &adc);

  ADC_RegularChannelConfig(ADC1, ADC_CHANNEL, 1, ADC_SampleTime_239Cycles5);

  ADC_Cmd(ADC1, ENABLE);
  ADC_ResetCalibration(ADC1);
  while (ADC_GetResetCalibrationStatus(ADC1))
    ;
  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1))
    ;
}

/******************************************************************************
 * USART1 中断 — 收字节，拼命令，检测到 \r/\n 后置标志
 ******************************************************************************/
void USART1_IRQHandler(void) {
  if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET) {
    uint8_t ch = USART_ReceiveData(USART1);

    if (cmd_idx < CMD_BUF_SIZE - 1) {
      cmd_buf[cmd_idx++] = ch;
    }

    if (ch == '\n' || ch == '\r') {
      cmd_buf[cmd_idx] = '\0';
      cmd_ready = 1;
      cmd_idx = 0;
    }

    USART_ClearITPendingBit(USART1, USART_IT_RXNE);
  }
}

/******************************************************************************
 * 解析AT指令
 ******************************************************************************/
static void ProcessCommand(const char *cmd) {
  if (cmd[0] == 'A' && cmd[1] == 'T') {
    char reply[64];
    if (str_has(cmd, "ADC")) {
      sprintf(reply, ">> %u\r\n", (unsigned int)adc_raw);
      Serial_SendString(reply);
    } else if (str_has(cmd, "Voltage")) {
      float v_adc = (float)adc_raw * ADC_REF_VOLTAGE / ADC_RESOLUTION;
      float v_in = v_adc * DIVIDER_RATIO;
      sprintf(reply, ">> %.2f\r\n", v_in);
      Serial_SendString(reply);
    }
  }
}

/******************************************************************************
 * 模糊查找
 ******************************************************************************/
static uint8_t str_has(const char *s, const char *keyword) {
  while (*s) {
    const char *a = s;
    const char *b = keyword;
    while (*a && *b && *a == *b) {
      a++;
      b++;
    }
    if (*b == '\0')
      return 1;
    s++;
  }
  return 0;
}

/******************************************************************************
 * 粗略延时（用于初始化阶段，ADC启动后短暂等待）
 ******************************************************************************/
static void Delay(volatile uint32_t count) {
  while (count--)
    ;
}