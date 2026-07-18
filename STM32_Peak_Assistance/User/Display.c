#include "Display.h"

void Display_Init(void) {
  MAX7219_Init();
  TM1637_Init();
}
void Display_Clear(void) {
  MAX7219_Clear();
  TM1637_Clear();
  LED_RED_Off();
  LED_GREEN_Off();
}

void Display_Channel(uint16_t channel) {
  uint8_t ch_high, ch_mid, ch_low;
  ch_high = channel / 100;
  ch_mid = (channel / 10) % 10;
  ch_low = channel % 10;

  if (channel >= 100) {
    TM1637_DisplayRaw(1, 0x39);
    TM1637_Display(2, ch_high, 0);
    TM1637_Display(3, ch_mid, 0);
    TM1637_Display(4, ch_low, 0);
  } else {
    TM1637_DisplayRaw(1, 0x39);
    TM1637_DisplayRaw(2, 0x76 | 0x80);
    if (channel < 10) {
      TM1637_ClearPos(3);
    } else {
      TM1637_Display(3, ch_mid, 0);
    }
    TM1637_Display(4, ch_low, 0);
  }
}

void Display_Voltage(double voltage, uint8_t is_mv) {
  uint8_t i;
  uint8_t digit_val;
  uint32_t int_part;

  if (voltage < 0)
    voltage = 0;

  MAX7219_Clear();
  TM1637_Clear();

  if (!is_mv) {
    LED_RED_Off();
    // 电压模式：MAX7219显示 XXX.XXXXX（固定格式，共8位）
    // digit1=百位, digit2=十位, digit3=个位(带点), digit4-8=小数5位
    if (voltage >= 1000.0) {
      // 超出范围，显示 "-.-.-.-.-"
      for (i = 1; i <= 8; i++)
        MAX7219_Write(i, 0x0A | ((i == 3 || i == 5 || i == 7) ? 0x80 : 0x00));
      TM1637_DisplayRaw(1, 0x40); // "-"
      TM1637_DisplayRaw(2, 0x40);
      TM1637_DisplayRaw(3, 0x40);
      TM1637_DisplayRaw(4, 0x40);
      return;
    }

    int_part = (uint32_t)voltage;

    // 整数部分3位（百位在前，高位为0也显示为0）
    digit_val = (uint8_t)((int_part / 100) % 10);
    MAX7219_Display(1, digit_val, 0);
    digit_val = (uint8_t)((int_part / 10) % 10);
    MAX7219_Display(2, digit_val, 0);
    digit_val = (uint8_t)(int_part % 10);
    MAX7219_Display(3, digit_val, 1); // 第三位后带小数点

    // 小数部分5位（依次乘10取整）
    double frac = voltage - (double)int_part;
    for (i = 0; i < 5; i++) {
      frac *= 10.0;
      digit_val = (uint8_t)((uint32_t)frac % 10);
      MAX7219_Display(4 + i, digit_val, 0);
    }

    // TM1637显示简化版：整数部分前3位 + 最后一位小数
    TM1637_Display(1, (uint8_t)((int_part / 100) % 10), 0);
    TM1637_Display(2, (uint8_t)((int_part / 10) % 10), 0);
    TM1637_Display(3, (uint8_t)(int_part % 10), 1);
    frac = voltage - (double)int_part;
    frac *= 10.0;
    TM1637_Display(4, (uint8_t)((uint32_t)frac % 10), 0);
  } else {
    // mV模式：顶格显示，小数点随数值自动
    LED_RED_On();
    int_part = (uint32_t)voltage;

    // 数整数部分位数
    uint8_t int_len;
    uint32_t temp = int_part;
    if (temp == 0) {
      int_len = 1;
    } else {
      int_len = 0;
      while (temp > 0 && int_len < 9) {
        int_len++;
        temp /= 10;
      }
    }

    if (int_len > 8) {
      // 溢出，显示全9
      for (i = 1; i <= 8; i++)
        MAX7219_Display(i, 9, 0);
      TM1637_DisplayRaw(1, 0x40);
      TM1637_DisplayRaw(2, 0x40);
      TM1637_DisplayRaw(3, 0x40);
      TM1637_DisplayRaw(4, 0x40);
      return;
    }

    // 计算整数部分高位除数（用于从左到右提取）
    uint32_t divisor = 1;
    for (i = 1; i < int_len; i++)
      divisor *= 10;

    // 从digit1（最左）开始填整数部分
    temp = int_part;
    for (i = 1; i <= int_len; i++) {
      digit_val = (uint8_t)((temp / divisor) % 10);
      MAX7219_Display(i, digit_val, 0);
      divisor /= 10;
    }

    // 在最后一位整数后加小数点（重写最后一位，带bit7）
    MAX7219_Display(int_len, (uint8_t)(int_part % 10), 1);

    // 剩余位填小数部分（digit int_len+1 到 digit 8）
    double frac = voltage - (double)int_part;
    for (i = int_len + 1; i <= 8; i++) {
      frac *= 10.0;
      digit_val = (uint8_t)((uint32_t)frac % 10);
      MAX7219_Display(i, digit_val, 0);
    }

    // TM1637显示前4位：前导零熄灭，右对齐显示数值
    // 如果整数部分 <= 3位，显示整数+1位小数；否则显示前4位整数
    if (int_len <= 3) {
      // 先全部熄灭
      for (i = 1; i <= 4; i++)
        TM1637_DisplayRaw(i, 0x00);
      // 从右向左填：位置4=小数第1位，位置3=个位(带点)，位置2=十位，位置1=百位
      // 先填小数位
      double tm_frac = voltage - (double)int_part;
      tm_frac *= 10.0;
      TM1637_Display(4, (uint8_t)((uint32_t)tm_frac % 10), 0);
      // 再从右向左填整数部分（个位带小数点）
      temp = int_part;
      for (i = 0; i < int_len; i++) {
        uint8_t dig = (uint8_t)(temp % 10);
        TM1637_Display(3 - i, dig, (i == 0) ? 1 : 0);
        temp /= 10;
      }
    } else {
      // 整数超过3位，显示前4位整数（无小数）
      uint32_t tm_div = 1;
      for (i = 1; i < 4; i++)
        tm_div *= 10;
      temp = int_part;
      for (i = 1; i <= 4; i++) {
        TM1637_Display(i, (uint8_t)((temp / tm_div) % 10), 0);
        tm_div /= 10;
      }
    }
  }
}

static void Display_4DigitVoltage(uint8_t start_digit, double voltage,
                                  uint8_t *is_mv_out) {
  uint32_t int_part;
  uint8_t int_len;
  uint32_t temp;
  uint32_t divisor;
  uint8_t i;
  uint8_t digit_val;
  double frac;
  uint8_t use_mv = 0;
  uint8_t dot_pos;
  uint8_t dec_places;

  if (voltage < 0)
    voltage = 0;

  if (voltage < 1.0) {
    voltage *= 1000.0;
    use_mv = 1;
  }

  int_part = (uint32_t)voltage;
  temp = int_part;
  if (temp == 0) {
    int_len = 1;
  } else {
    int_len = 0;
    while (temp > 0 && int_len < 9) {
      int_len++;
      temp /= 10;
    }
  }

  if (use_mv) {
    if (int_len > 4) {
      for (i = 1; i <= 4; i++)
        MAX7219_Display(start_digit + i - 1, 9, 0);
    } else {
      // mV模式：前导零熄灭，仅显示有效数值
      // 例如 11mV → 显示 "  11"（前两位熄灭，后两位显示11）
      // 用法：先全部熄灭，再从右向左填入有效数字
      uint8_t pos;
      // 先全部熄灭（写0x0F=blank，BCD译码模式下）
      for (pos = 0; pos < 4; pos++) {
        MAX7219_Write(MAX7219_RevPos(start_digit + pos), 0x0F);
      }
      // 从最右位开始向左填入有效数字
      temp = int_part;
      for (pos = 0; pos < int_len; pos++) {
        digit_val = (uint8_t)(temp % 10);
        MAX7219_Write(MAX7219_RevPos(start_digit + 3 - pos), digit_val);
        temp /= 10;
      }
    }
    if (is_mv_out)
      *is_mv_out = 1;
    return;
  }

  if (voltage >= 1000.0) {
    for (i = 1; i <= 4; i++)
      MAX7219_Display(start_digit + i - 1, 9, 0);
    if (is_mv_out)
      *is_mv_out = 0;
    return;
  }

  if (int_len >= 3) {
    int_len = 3;
    dec_places = 1;
    dot_pos = 3;
  } else if (int_len == 2) {
    dec_places = 2;
    dot_pos = 2;
  } else {
    dec_places = 3;
    dot_pos = 1;
  }

  divisor = 1;
  for (i = 1; i < int_len; i++)
    divisor *= 10;
  temp = int_part;
  for (i = 1; i <= int_len; i++) {
    digit_val = (uint8_t)((temp / divisor) % 10);
    MAX7219_Display(start_digit + i - 1, digit_val, (i == dot_pos) ? 1 : 0);
    divisor /= 10;
  }

  frac = voltage - (double)int_part;
  for (i = 1; i <= dec_places; i++) {
    frac *= 10.0;
    digit_val = (uint8_t)((uint32_t)frac % 10);
    MAX7219_Display(start_digit + int_len + i - 1, digit_val, 0);
  }

  if (is_mv_out)
    *is_mv_out = 0;
}

void Display_PeakVoltage(double voltage_peak, double voltage_threshold) {
  uint8_t peak_mv = 0;
  uint8_t th_mv = 0;

  // 直接覆盖写入，不再预清屏（避免闪烁；BCD模式下覆盖写也能正确显示）
  Display_4DigitVoltage(1, voltage_peak, &peak_mv);
  Display_4DigitVoltage(5, voltage_threshold, &th_mv);

  if (peak_mv || th_mv)
    LED_RED_On();
  else
    LED_RED_Off();
}

/**
 * @brief 显示 "NO SIG" 占位符
 *
 * 当无输入电压时显示此内容，证明数码管工作正常。
 * MAX7219 显示 "NO SIG"，TM1637 显示 "NO."
 *
 * MAX7219 段排列(不译码模式):
 *   bit7=DP, bit6=A, bit5=B, bit4=C, bit3=D, bit2=E, bit1=F, bit0=G
 * 共阴极，1=亮。
 *
 * 注意：MAX7219_Display() 内部已通过 RevPos 反转地址，
 * 所以 digit=1 表示最左位，digit=8 表示最右位。
 *
 * 字符段码:
 *   N: A,B,C,E,F,G -> 0b01110111 = 0x77
 *   O: A,B,C,D,E,F -> 0b01111110 = 0x7E
 *   (空格): 全灭   -> 0x00
 *   S: A,C,D,F,G   -> 0b01101101 = 0x6D
 *   I: B,C         -> 0b00110000 = 0x30
 *   G: A,B,C,D,F   -> 0b01111101 = 0x7D
 *   .: DP          -> 0x80
 */
void Display_NoInput(void) {
  // MAX7219: 切换到不译码模式，手动写段码
  MAX7219_Write(MAX7219_DECODEMODE, 0x00);

  // digit 1-4 = 左4位, digit 5-8 = 右4位
  // 左4位: [N] [O] [ ] [S]
  MAX7219_Display(1, 0x77, 0); // N, 无小数点
  MAX7219_Display(2, 0x7E, 0); // O
  MAX7219_Display(3, 0x00, 0); // 空格
  MAX7219_Display(4, 0x6D, 0); // S

  // 右4位: [I] [G] [ ] [.]
  MAX7219_Display(5, 0x30, 0); // I
  MAX7219_Display(6, 0x7D, 0); // G
  MAX7219_Display(7, 0x00, 0); // 空格
  MAX7219_Display(8, 0x80, 0); // . (DP=0x80, 段码全灭仅DP亮)

  // 切回 BCD 译码模式（供后续正常显示使用）
  MAX7219_Write(MAX7219_DECODEMODE, 0xFF);

  // TM1637 显示 "NO.."
  // TM1637 段: bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G, bit7=DP
  // N: A,B,C,E,F,G -> 0b01110111 = 0x77
  // O: A,B,C,D,E,F -> 0b01111110 = 0x7E
  // .: DP          -> 0x80
  TM1637_DisplayRaw(1, 0x77); // N
  TM1637_DisplayRaw(2, 0x7E); // O
  TM1637_DisplayRaw(3, 0x80); // .
  TM1637_DisplayRaw(4, 0x80); // .
}

void Display_QuickSelfTest(uint16_t channel) {
  uint8_t i, j;
  uint8_t ch_high, ch_mid, ch_low;

  MAX7219_Clear();
  LED_RED_Off();
  LED_GREEN_Off();

  ch_high = channel / 100;
  ch_mid = (channel / 10) % 10;
  ch_low = channel % 10;

  if (channel >= 100) {
    TM1637_DisplayRaw(1, 0x39);
    TM1637_Display(2, ch_high, 0);
    TM1637_Display(3, ch_mid, 0);
    TM1637_Display(4, ch_low, 0);
  } else {
    TM1637_DisplayRaw(1, 0x39);
    TM1637_DisplayRaw(2, 0x76 | 0x80);
    if (channel < 10) {
      TM1637_ClearPos(3);
    } else {
      TM1637_Display(3, ch_mid, 0);
    }
    TM1637_Display(4, ch_low, 0);
  }

  LED_GREEN_On();
  for (i = 1; i <= 8; i++) {
    MAX7219_Display(i, 8, 1);
    Delay_ms(80);
  }
  for (i = 8; i >= 1; i--) {
    MAX7219_ClearPos(i);
    Delay_ms(80);
  }

  for (j = 0; j < 10; j++) {
    for (i = 1; i <= 8; i++)
      MAX7219_Display(i, j, 0);
    Delay_ms(80);
  }

  for (j = 0; j < 10; j++) {
    for (i = 1; i <= 8; i++)
      MAX7219_Display(i, (j + i) % 10, (i == 4) ? 1 : 0);
    Delay_ms(60);
  }

  for (j = 0; j < 4; j++) {
    for (i = 1; i <= 8; i++)
      MAX7219_Display(i, i, 1);
    Delay_ms(100);
    for (i = 1; i <= 8; i++)
      MAX7219_Display(i, i, 0);
    Delay_ms(100);
  }

  LED_GREEN_Off();
  MAX7219_Clear();
}

void Display_DigitTest(void) {
  uint8_t i, j;

  // 跑马灯：MAX7219每一位依次点亮
  MAX7219_Clear();
  for (i = 1; i <= 8; i++) {
    MAX7219_Display(i, 8, 1);
    Delay_ms(150);
    MAX7219_ClearPos(i);
  }
  for (i = 8; i >= 1; i--) {
    MAX7219_Display(i, 8, 1);
    Delay_ms(150);
    MAX7219_ClearPos(i);
  }

  // 跑马灯：TM1637每一位依次点亮
  TM1637_Clear();
  for (i = 1; i <= 4; i++) {
    TM1637_Display(i, 8, 1);
    Delay_ms(200);
    TM1637_ClearPos(i);
  }
  for (i = 4; i >= 1; i--) {
    TM1637_Display(i, 8, 1);
    Delay_ms(200);
    TM1637_ClearPos(i);
  }

  // 数字轮询：MAX7219所有位同步显示 0-9
  for (j = 0; j < 10; j++) {
    for (i = 1; i <= 8; i++) {
      MAX7219_Display(i, j, 0);
    }
    Delay_ms(300);
  }

  // 数字轮询：TM1637所有位同步显示 0-9
  for (j = 0; j < 10; j++) {
    for (i = 1; i <= 4; i++) {
      TM1637_Display(i, j, 0);
    }
    Delay_ms(300);
  }

  // 逐位数字轮询：MAX7219 0-9依次滚动
  MAX7219_Clear();
  for (j = 0; j < 20; j++) {
    for (i = 1; i <= 8; i++) {
      MAX7219_Display(i, (j + i) % 10, (i == 4) ? 1 : 0);
    }
    Delay_ms(150);
  }

  // 逐位数字轮询：TM1637 0-9依次滚动
  TM1637_Clear();
  for (j = 0; j < 20; j++) {
    for (i = 1; i <= 4; i++) {
      TM1637_Display(i, (j + i) % 10, (i == 2) ? 1 : 0);
    }
    Delay_ms(150);
  }

  // 小数点/冒号闪烁测试
  MAX7219_Clear();
  TM1637_Clear();
  for (i = 1; i <= 8; i++)
    MAX7219_Display(i, i, 0);
  for (i = 1; i <= 4; i++)
    TM1637_Display(i, i, 0);

  for (j = 0; j < 6; j++) {
    for (i = 1; i <= 8; i++)
      MAX7219_Display(i, i, 1);
    for (i = 1; i <= 4; i++)
      TM1637_SetColon(i, 1);
    Delay_ms(300);

    for (i = 1; i <= 8; i++)
      MAX7219_Display(i, i, 0);
    for (i = 1; i <= 4; i++)
      TM1637_SetColon(i, 0);
    Delay_ms(300);
  }

  MAX7219_Clear();
  TM1637_Clear();
}

void Test_MAX7219(void) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    MAX7219_Display(i + 1, (i + 1) % 10, 0);
    Delay_ms(200);
  }
  MAX7219_Display(1, 8, 1);
  MAX7219_Display(2, 8, 1);
  MAX7219_Display(3, 8, 1);
  MAX7219_Display(4, 8, 1);
  MAX7219_Display(5, 8, 1);
  MAX7219_Display(6, 8, 1);
  MAX7219_Display(7, 8, 1);
  MAX7219_Display(8, 8, 1);
  Delay_ms(1000);
  MAX7219_Clear();
  Delay_ms(500);
}

void Test_TM1637(void) {
  uint8_t i;
  for (i = 0; i < 10; i++) {
    TM1637_Display(1, i, 0);
    TM1637_Display(2, i, 0);
    TM1637_Display(3, i, 0);
    TM1637_Display(4, i, 0);
    Delay_ms(200);
  }
  TM1637_Display4Num(1234, 1);
  Delay_ms(1000);
  TM1637_Display4Num(5678, 0);
  Delay_ms(1000);
  TM1637_Clear();
  Delay_ms(500);
}

void Test_ChannelCtl(void) {
  ChannelSet_600V();
  Delay_ms(1000);
  ChannelSet_250V();
  Delay_ms(1000);
  ChannelSet_36V();
  Delay_ms(1000);
  ChannelSet_5V();
  Delay_ms(1000);
}