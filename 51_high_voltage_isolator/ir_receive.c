#include "ir_receive.h"

// 红外接收相关变量
unsigned char ir_raw_data[4];  // 存储红外接收的原始数据，共4字节
bit ir_receive_complete = 0;   // 红外接收完成标志
unsigned char ir_key_code = 0; // 解码后的按键编码

// 定时器0计数变量
unsigned int time_count = 0;
// 数据位计数
unsigned char bit_count = 0;
// 接收状态
unsigned char ir_state = 0;

// 当主进程获取后清除
uint8_t receive_flag = 0;

// [DBG] 00 FF 30 CF Key:0x30
// Unknown Key
// [DBG] 00 FF 18 E7 Key:0x18
// Running Light ON
// [DBG] 00 FF 7A 85 Key:0x7A
// Unknown Key
// [DBG] 00 FF 10 EF Key:0x10
// Unknown Key
// [DBG] 00 FF 90 6F Key:0x90
// Unknown Key
// [DBG] 00 FF A8 57 Key:0xA8
// Unknown Key
// [DBG] 00 FF 02 FD Key:0x02
// Unknown Key
// [DBG] 00 FF C2 3D Key:0xC2
// Unknown Key

// 按键编码映射表（根据实际遥控器调整）
code unsigned char IR_KEY_MAP[] = {
    0x00, // 无按键
    0x30, // 1 (CH1继电器)
    0x18, // 2 (CH2继电器)
    0x7A, // 3 (CH3继电器)
    0x10, // 4 (CH4继电器)
    0x90, // 5 (上键)
    0xA8, // 6 (下键)
    0x02, // 7 (左键)
    0xC2, // 8 (右键)
    0x00, // 9 (未使用)
    0x00  // 10 (未使用)
};

// 初始化红外接收
void IR_Init(void) {
  IT0 = 1; // INT0(P3.2)下降沿触发
  EX0 = 1; // 使能INT0中断
  EA = 1;

  // 初始化定时器0，用于测量脉冲宽度
  TMOD &= 0xF0;
  TMOD |= 0x01; // 定时器0工作在模式1 (16位定时器)
  TH0 = 0;
  TL0 = 0;
  TR0 = 1; // 启动定时器连续计数，每次下降沿读取并复位
}

void IR_ISR(void) interrupt 0 {
  uint16_t pulse_width;

  pulse_width = (TH0 << 8) | TL0; // 读取两次下降沿之间的时间
  TH0 = 0;
  TL0 = 0; // 复位定时器

  // 判断脉冲类型（11.0592MHz, 12T模式: 1tick ≈ 1.085µs）
  if (pulse_width > 11000 && pulse_width < 14000) {
    // 引导码: 9ms LOW + 4.5ms HIGH ≈ 13.5ms → ~12442 ticks
    bit_count = 0;
    ir_state = 1;
  } else if (ir_state == 1) {
    // 数据位
    if (pulse_width > 1800 && pulse_width < 2500) {
      // 位1: 560µs LOW + 1.69ms HIGH ≈ 2.25ms → ~2074 ticks
      ir_raw_data[bit_count / 8] |= (0x80 >> (bit_count % 8));
      bit_count++;
    } else if (pulse_width > 800 && pulse_width < 1300) {
      // 位0: 560µs LOW + 560µs HIGH ≈ 1.12ms → ~1032 ticks
      ir_raw_data[bit_count / 8] &= ~(0x80 >> (bit_count % 8));
      bit_count++;
    } else {
      // 无效脉冲（含重复码等情况），复位
      ir_state = 0;
      bit_count = 0;
    }

    if (bit_count >= 32) {
      // 验证数据有效性（地址码与地址反码、数据码与数据反码应互补）
      if ((ir_raw_data[0] ^ ir_raw_data[1]) == 0xFF &&
          (ir_raw_data[2] ^ ir_raw_data[3]) == 0xFF) {
        ir_key_code = ir_raw_data[2];
        ir_receive_complete = 1;
      }
      ir_state = 0;
      bit_count = 0;
    }
  }
  // 其他情况（第一个下降沿或噪声）忽略
}

// 获取解码后的按键编码
unsigned char IR_GetKeyCode(void) {
  if (ir_receive_complete) {
    ir_receive_complete = 0; // 清除标志
    receive_flag = 1;        // 设置接收完成标志
    return ir_key_code;
  }
  return 0; // 无按键
}

// 获取数字按键值（0-9）
unsigned char IR_GetDigit(void) {
  unsigned char key_code = IR_GetKeyCode();
  unsigned char i;

  // 查找按键编码对应的数字
  for (i = 0; i < 11; i++) {
    if (IR_KEY_MAP[i] == key_code) {
      return i; // 返回数字（0-9）
    }
  }
  return 0xFF; // 非数字按键
}

uint8_t get_receive_flag() { return receive_flag; }

void clear_receive_flag() { receive_flag = 0; }

// 通过串口发送红外数据
void IR_SendData(void) {
  if (ir_receive_complete) {
    UART_SendString("IR Data: ");
    UART_SendHex(ir_raw_data[0]); // 地址码
    UART_SendByte(' ');
    UART_SendHex(ir_raw_data[1]); // 地址反码
    UART_SendByte(' ');
    UART_SendHex(ir_raw_data[2]); // 数据码
    UART_SendByte(' ');
    UART_SendHex(ir_raw_data[3]); // 数据反码
    UART_SendString(" Key: ");
    UART_SendHex(ir_key_code); // 按键编码
    UART_SendString("\r\n");
    ir_receive_complete = 0; // 清除标志
  }
}