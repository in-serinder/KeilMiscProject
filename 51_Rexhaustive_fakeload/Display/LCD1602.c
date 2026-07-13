#include "stc89c52.h"

// 引脚配置：
#define LCD_RS P15
#define LCD_RW P16
#define LCD_EN P17
#define LCD_DataPort P0

// 函数定义：

/**
 * @brief  LCD1602延时函数，12MHz调用可延时1ms
 * @param  无
 * @retval 无
 */
void LCD_Delay() {
  unsigned char xdata dly_i = 5;
  unsigned char xdata dly_j = 248;
  do {
    dly_j = 248;
    while (--dly_j)
      ;
  } while (--dly_i);
}

/**
 * @brief  LCD1602长延时，约5ms（12MHz）
 */
void LCD_LongDelay() {
  unsigned char xdata dly_i, dly_j;
  for (dly_i = 0; dly_i < 5; dly_i++) {
    dly_j = 248;
    while (--dly_j)
      ;
    dly_j = 248;
    while (--dly_j)
      ;
    dly_j = 248;
    while (--dly_j)
      ;
    dly_j = 248;
    while (--dly_j)
      ;
    dly_j = 248;
    while (--dly_j)
      ;
  }
}

/**
 * @brief  LCD1602写命令
 * @param  Command 要写入的命令
 * @retval 无
 */
void LCD_WriteCommand(unsigned char Command) {
  LCD_RS = 0;
  LCD_RW = 0;
  LCD_DataPort = Command;
  LCD_EN = 1;
  LCD_Delay();
  LCD_EN = 0;
  LCD_Delay();
}

/**
 * @brief  LCD1602写数据
 * @param  Data 要写入的数据
 * @retval 无
 */
void LCD_WriteData(unsigned char Data) {
  LCD_RS = 1;
  LCD_RW = 0;
  LCD_DataPort = Data;
  LCD_EN = 1;
  LCD_Delay();
  LCD_EN = 0;
  LCD_Delay();
}

/**
 * @brief  LCD1602设置光标位置
 * @param  Line 行位置，范围：1~2
 * @param  Column 列位置，范围：1~16
 * @retval 无
 */
void LCD_SetCursor(unsigned char Line, unsigned char Column) {
  if (Line == 1) {
    LCD_WriteCommand(0x80 | (Column - 1));
  } else if (Line == 2) {
    LCD_WriteCommand(0x80 | (Column - 1 + 0x40));
  }
}

/**
 * @brief  LCD1602初始化函数
 * @param  无
 * @retval 无
 */
void LCD_Init() {
  // 上电后等待 >15ms 让LCD稳定
  LCD_LongDelay();
  LCD_LongDelay();
  LCD_LongDelay();

  // 按照HD44780规格书，0x38需要发送3次确保可靠
  LCD_WriteCommand(0x38); // 八位数据接口，两行显示，5*7点阵
  LCD_LongDelay();        // 等待 >4.1ms
  LCD_WriteCommand(0x38);
  LCD_Delay(); // 等待 >100us
  LCD_WriteCommand(0x38);
  LCD_Delay();

  LCD_WriteCommand(0x0c); // 显示开，光标关，闪烁关
  LCD_Delay();
  LCD_WriteCommand(0x01); // 光标复位，清屏（需要最长延时）
  LCD_LongDelay();        // 清屏需要 >1.64ms，给足5ms
  LCD_WriteCommand(0x06); // 数据读写操作后，光标自动加一，画面不动
  LCD_Delay();
}

/**
 * @brief  LCD1602清屏函数
 * @param  无
 * @retval 无
 */
void LCD_Clear() { LCD_WriteCommand(0x01); }

/**
 * @brief  在LCD1602指定位置上显示一个字符
 * @param  Line 行位置，范围：1~2
 * @param  Column 列位置，范围：1~16
 * @param  Char 要显示的字符
 * @retval 无
 */
void LCD_ShowChar(unsigned char Line, unsigned char Column, char Char) {
  LCD_SetCursor(Line, Column);
  LCD_WriteData(Char);
}

/**
 * @brief  在LCD1602指定位置开始显示所给字符串
 * @param  Line 起始行位置，范围：1~2
 * @param  Column 起始列位置，范围：1~16
 * @param  String 要显示的字符串
 * @retval 无
 */
void LCD_ShowString(unsigned char Line, unsigned char Column, char *String) {
  unsigned char xdata idx;
  LCD_SetCursor(Line, Column);
  for (idx = 0; String[idx] != '\0'; idx++) {
    LCD_WriteData(String[idx]);
  }
}

/**
 * @brief  返回值=X的Y次方
 */
int LCD_Pow(int X, int Y) {
  int xdata Result = 1;
  unsigned char xdata p;
  for (p = 0; p < Y; p++) {
    Result *= X;
  }
  return Result;
}

/**
 * @brief  在LCD1602指定位置开始显示所给数字
 * @param  Line 起始行位置，范围：1~2
 * @param  Column 起始列位置，范围：1~16
 * @param  Number 要显示的数字，范围：0~65535
 * @param  Length 要显示数字的长度，范围：1~5
 * @retval 无
 */
void LCD_ShowNum(unsigned char Line, unsigned char Column, unsigned int Number,
                 unsigned char Length) {
  unsigned char xdata n;
  LCD_SetCursor(Line, Column);
  for (n = Length; n > 0; n--) {
    LCD_WriteData(Number / LCD_Pow(10, n - 1) % 10 + '0');
  }
}

/**
 * @brief  在LCD1602指定位置开始以有符号十进制显示所给数字
 * @param  Line 起始行位置，范围：1~2
 * @param  Column 起始列位置，范围：1~16
 * @param  Number 要显示的数字，范围：-32768~32767
 * @param  Length 要显示数字的长度，范围：1~5
 * @retval 无
 */
// void LCD_ShowSignedNum(unsigned char Line, unsigned char Column, int Number,
//                        unsigned char Length) {
//   unsigned char i;
//   unsigned int Number1;
//   LCD_SetCursor(Line, Column);
//   if (Number >= 0) {
//     LCD_WriteData('+');
//     Number1 = Number;
//   } else {
//     LCD_WriteData('-');
//     Number1 = -Number;
//   }
//   for (i = Length; i > 0; i--) {
//     LCD_WriteData(Number1 / LCD_Pow(10, i - 1) % 10 + '0');
//   }
// }

// /**
//  * @brief  在LCD1602指定位置开始以十六进制显示所给数字
//  * @param  Line 起始行位置，范围：1~2
//  * @param  Column 起始列位置，范围：1~16
//  * @param  Number 要显示的数字，范围：0~0xFFFF
//  * @param  Length 要显示数字的长度，范围：1~4
//  * @retval 无
//  */
// void LCD_ShowHexNum(unsigned char Line, unsigned char Column,
//                     unsigned int Number, unsigned char Length) {
//   unsigned char i, SingleNumber;
//   LCD_SetCursor(Line, Column);
//   for (i = Length; i > 0; i--) {
//     SingleNumber = Number / LCD_Pow(16, i - 1) % 16;
//     if (SingleNumber < 10) {
//       LCD_WriteData(SingleNumber + '0');
//     } else {
//       LCD_WriteData(SingleNumber - 10 + 'A');
//     }
//   }
// }

/**
 * @brief  在LCD1602指定位置开始以二进制显示所给数字
 * @param  Line 起始行位置，范围：1~2
 * @param  Column 起始列位置，范围：1~16
 * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
 * @param  Length 要显示数字的长度，范围：1~16
 * @retval 无
 */
// void LCD_ShowBinNum(unsigned char Line, unsigned char Column,
//                     unsigned int Number, unsigned char Length) {
//   unsigned char i;
//   LCD_SetCursor(Line, Column);
//   for (i = Length; i > 0; i--) {
//     LCD_WriteData(Number / LCD_Pow(2, i - 1) % 2 + '0');
//   }
// }