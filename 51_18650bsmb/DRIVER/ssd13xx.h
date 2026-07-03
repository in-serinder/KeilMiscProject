#ifndef __SSD13XX_H__
#define __SSD13XX_H__

#include "stc15w.h" // 根据实际头文件路径修改
#include <string.h>

// I2C引脚定义
#define SDA P07
#define SCL P06

// OLED屏幕参数
#define SSD13XX_WIDTH 128
#define SSD13XX_HEIGHT 64
#define SSD13XX_PAGE_NUM 8
#define SSD13XX_PAGE_WIDTH 128

// I2C地址
#define SSD13XX_I2C_ADDRESS 0x3C

// 控制字节
#define SSD13XX_CONTROL_BYTE_CMD_SINGLE 0x80
#define SSD13XX_CONTROL_BYTE_CMD_STREAM 0x00
#define SSD13XX_CONTROL_BYTE_DATA_STREAM 0x40

// 常用命令
#define SSD13XX_CMD_DISPLAY_OFF 0xAE
#define SSD13XX_CMD_DISPLAY_ON 0xAF
#define SSD13XX_CMD_SET_DISPLAY_CLOCK_DIV 0xD5
#define SSD13XX_CMD_SET_MULTIPLEX 0xA8
#define SSD13XX_CMD_SET_DISPLAY_OFFSET 0xD3
#define SSD13XX_CMD_SET_START_LINE 0x40
#define SSD13XX_CMD_CHARGE_PUMP 0x8D
#define SSD13XX_CMD_MEMORY_MODE 0x20
#define SSD13XX_CMD_SEGREMAP 0xA1
#define SSD13XX_CMD_COMSCAN_DEC 0xC8
#define SSD13XX_CMD_COMSCAN_INC 0xC0
#define SSD13XX_CMD_SET_COMPINS 0xDA
#define SSD13XX_CMD_SET_CONTRAST 0x81
#define SSD13XX_CMD_SET_PRECHARGE 0xD9
#define SSD13XX_CMD_SET_VCOMDETECT 0xDB
#define SSD13XX_CMD_DISPLAYALLON_RESUME 0xA4
#define SSD13XX_CMD_DISPLAYALLON 0xA5
#define SSD13XX_CMD_NORMAL_DISPLAY 0xA6
#define SSD13XX_CMD_INVERT_DISPLAY 0xA7
#define SSD13XX_CMD_DEACTIVATE_SCROLL 0x2E
#define SSD13XX_CMD_ACTIVATE_SCROLL 0x2F
#define SSD13XX_CMD_COLUMN_ADDR 0x21
#define SSD13XX_CMD_PAGE_ADDR 0x22

// 字体大小定义
typedef enum { FONT_8X8 = 0, FONT_16X16 = 1 } FontSize;

// 基础I2C函数（底层）
void I2C_Init(void);
void I2C_Start(void);
void I2C_Stop(void);
void I2C_SendByte(unsigned char byte);
bit I2C_WaitAck(void);

// SSD13XX 基本命令/数据发送
void SSD13XX_WriteCommand(unsigned char command);
void SSD13XX_WriteData(unsigned char dat);

// 页管理
void SSD13XX_FlushPage(unsigned char page); // 将当前 PageBuffer 发送到指定页
void SSD13XX_ClearPage(unsigned char page); // 清空指定页（全0）
void SSD13XX_Clear(void);                   // 清空所有页

// 图形绘制（自动跨页）
void SSD13XX_DrawPixel(unsigned char x, unsigned char y, unsigned char color);
void SSD13XX_DrawLine(unsigned char x1, unsigned char y1, unsigned char x2,
                      unsigned char y2, unsigned char color);
void SSD13XX_DrawRectangle(unsigned char x1, unsigned char y1, unsigned char x2,
                           unsigned char y2, unsigned char color);
void SSD13XX_DrawCircle(unsigned char x0, unsigned char y0, unsigned char r,
                        unsigned char color);
void SSD13XX_DrawBitmap(unsigned char x, unsigned char y,
                        const unsigned char *bitmap, unsigned char width,
                        unsigned char height);

// 文本显示（直接发送，无需缓冲区）
void SSD13XX_WriteChar(unsigned char x, unsigned char y, char c, FontSize size);
void SSD13XX_WriteString(unsigned char x, unsigned char y, char *str,
                         FontSize size);

// 初始化
void SSD13XX_Init(void);

void SSD13XX_ShowAllChars(void);

void Draw_ProgressBar_Component(unsigned char x, unsigned char y,
                                unsigned char width, char *left_str,
                                char *right_str, unsigned char percent);
void Draw_ProgressBar_Double(unsigned char x, unsigned char y,
                             unsigned char width, char *title,
                             char *title_right, char *label,
                             unsigned char percent);

// 删除无效的自引用别名定义

#endif