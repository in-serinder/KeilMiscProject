#ifndef __12864G_H
#define __12864G_H

#include "stm32f10x.h"
#include "Delay.h"

/* 类型定义（兼容原 8051 代码风格） */
typedef unsigned char  uchar;
typedef unsigned int   uint;
typedef unsigned long  ulong;

/*============================================================================
  LCD 接口引脚定义（可根据实际接线修改）
  原 8051: lcd_sclk=P3^2  lcd_sid=P3^1  lcd_rs=P3^0  lcd_reset=P1^0  lcd_cs1=P1^1
============================================================================*/
#define LCD_SCLK_PORT    GPIOA
#define LCD_SCLK_PIN     GPIO_Pin_0
#define LCD_SID_PORT     GPIOA
#define LCD_SID_PIN      GPIO_Pin_1
#define LCD_RS_PORT      GPIOA
#define LCD_RS_PIN       GPIO_Pin_2
#define LCD_RESET_PORT   GPIOA
#define LCD_RESET_PIN    GPIO_Pin_3
#define LCD_CS1_PORT     GPIOA
#define LCD_CS1_PIN      GPIO_Pin_4

/*============================================================================
  字库IC接口引脚定义（JLX-GB2312）
  原 8051: Rom_IN=P1^2  Rom_OUT=P1^3  Rom_SCK=P1^4  Rom_CS=P1^5
============================================================================*/
#define ROM_IN_PORT      GPIOB
#define ROM_IN_PIN       GPIO_Pin_12
#define ROM_OUT_PORT     GPIOB
#define ROM_OUT_PIN      GPIO_Pin_13
#define ROM_SCK_PORT     GPIOB
#define ROM_SCK_PIN      GPIO_Pin_14
#define ROM_CS_PORT      GPIOB
#define ROM_CS_PIN       GPIO_Pin_15

/* LCD 引脚操作宏 */
#define LCD_SCLK_H()   GPIO_SetBits(LCD_SCLK_PORT, LCD_SCLK_PIN)
#define LCD_SCLK_L()   GPIO_ResetBits(LCD_SCLK_PORT, LCD_SCLK_PIN)
#define LCD_SID_H()    GPIO_SetBits(LCD_SID_PORT, LCD_SID_PIN)
#define LCD_SID_L()    GPIO_ResetBits(LCD_SID_PORT, LCD_SID_PIN)
#define LCD_RS_H()     GPIO_SetBits(LCD_RS_PORT, LCD_RS_PIN)
#define LCD_RS_L()     GPIO_ResetBits(LCD_RS_PORT, LCD_RS_PIN)
#define LCD_RESET_H()  GPIO_SetBits(LCD_RESET_PORT, LCD_RESET_PIN)
#define LCD_RESET_L()  GPIO_ResetBits(LCD_RESET_PORT, LCD_RESET_PIN)
#define LCD_CS1_H()    GPIO_SetBits(LCD_CS1_PORT, LCD_CS1_PIN)
#define LCD_CS1_L()    GPIO_ResetBits(LCD_CS1_PORT, LCD_CS1_PIN)

/* 字库IC引脚操作宏 */
#define ROM_IN_H()     GPIO_SetBits(ROM_IN_PORT, ROM_IN_PIN)
#define ROM_IN_L()     GPIO_ResetBits(ROM_IN_PORT, ROM_IN_PIN)
#define ROM_SCK_H()    GPIO_SetBits(ROM_SCK_PORT, ROM_SCK_PIN)
#define ROM_SCK_L()    GPIO_ResetBits(ROM_SCK_PORT, ROM_SCK_PIN)
#define ROM_CS_H()     GPIO_SetBits(ROM_CS_PORT, ROM_CS_PIN)
#define ROM_CS_L()     GPIO_ResetBits(ROM_CS_PORT, ROM_CS_PIN)
#define ROM_OUT_READ() GPIO_ReadInputDataBit(ROM_OUT_PORT, ROM_OUT_PIN)

/* 函数声明 */
void LCD_GPIO_Init(void);
void LCD_Init(void);
void LCD_Clear(void);
void LCD_Address(uint page, uint column);
void Transfer_Command_LCD(uchar data1);
void Transfer_Data_LCD(uchar data1);

void Display_128x64(const uchar *dp);
void Display_Graphic_16x16(uchar page, uchar column, const uchar *dp);
void Display_Graphic_8x16(uchar page, uchar column, const uchar *dp);
void Display_Graphic_5x8(uchar page, uchar column, const uchar *dp);

void Send_Command_To_ROM(uchar datu);
uchar Get_Data_From_ROM(void);
void Get_And_Write_16x16(ulong fontaddr, uchar page, uchar column);
void Get_And_Write_8x16(ulong fontaddr, uchar page, uchar column);
void Get_And_Write_5x8(ulong fontaddr, uchar page, uchar column);

void Display_GB2312_String(uchar page, uchar column, uchar *text);
void Display_String_5x8(uchar page, uchar column, uchar *text);

#endif