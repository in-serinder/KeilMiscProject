#include "Delay.h"
#include "Display.h"
#include "LCD1602.h"
#include "System/Timer.h"
#include "System/UART.h"
#include "System/stc89c52.h"
#include "System/stm32cmdADC.h"
#include "encode.h"
#include "fakeload.h"
#include "key.h"
#include "peripheral.h"

// Keil C51 does not support stdint.h, using native types
// void vector_lock(void) code_at 0x0000 {
// goto main;             // 0000 复位
// goto EC11_A_Triggered; // 0003 INT0
// goto Timer0_ISR;       // 000B T0 关键保护区域
//  ;                      // 填充剩余字节
//}
#define BEEP_SEC 3U
#define SAFE_VOLTAGE 37.0f
// 编码器旋转只去变动一个功耗索引数值
uint8_t idata loadIndex = 0;
uint16_t idata buzzer_tick = 0;
uint16_t idata duration_time_seconds = 0;
// 运行模式专用变量
static uint8_t idata last_loadIndex_run = 0xFF; // 上次设置的负载索引
static uint16_t idata last_run_tick = 0;        // 上次刷新时的系统滴答值
uint8_t idata encFlag = 0; // bit0:模式切换；bit1:编码器旋转
uint8_t idata encDir = 0;
// 使用static避免与Display.c的buffer重复定义
static uint8_t idata buffer[20];
bit idata encKeyPress = 0;
float idata voltage = 0.0f;
float idata power = 0.0f;

// 调整值
bit is_adjusting_duration_time =
    0; // 是否正在调整时间 0:调整功率负载 1:调整持续时间
bit is_running = 0;

// 启停按键防抖计数器（非阻塞扫描用）
uint8_t idata start_key_debounce = 0;

// 过压状态跟踪（避免每10ms重复显示错误）
bit prev_over_voltage = 0;
// 浮空状态跟踪
bit idata input_floating = 0;

// 空闲超时（1分钟无操作进入空闲屏幕）
#define IDLE_TIMEOUT_TICKS 6000 // 6000 * 10ms = 60秒
uint16_t idata idle_counter = 0;
bit idata is_idle = 0;

// 自动接收STM32电压数据（中断方式）
char idata rx_voltage_str[8];     // 中断中存储的电压数字字符串
bit idata voltage_updated = 0;    // 电压更新标志
bit idata voltage_valid = 0;      // 是否已收到有效电压数据
float idata prev_voltage = -1.0f; // 上次显示的电压值（初始化为-1确保首次更新）
uint16_t idata auto_rx_timeout =
    0; // 自动接收超时计数（约2秒无数据启用后备查询）

// 显示刷新节流（避免频繁刷屏）
uint8_t idata display_refresh_tick = 0;

// UART中断服务函数 - 接收STM32自动发送的电压数据
// 格式：">> X.XX\r\n" 或 ">> XX.X\r\n"
void UART_IRQHandler(void) interrupt 4 {
  static unsigned char idata rx_state = 0;
  static unsigned char idata rx_idx = 0;
  unsigned char ch;

  if (RI) {
    RI = 0;
    ch = SBUF;

    // 状态机：匹配 ">> " 前缀后提取数字字符串
    if (rx_state == 0 && ch == '>') {
      rx_state = 1;
    } else if (rx_state == 1 && ch == '>') {
      rx_state = 2;
      rx_idx = 0;
    } else if (rx_state == 2) {
      if (ch == '\r' || ch == '\n') {
        // 收到结束符，标记数据就绪
        rx_voltage_str[rx_idx] = '\0';
        voltage_updated = 1;
        rx_state = 0;
      } else if (ch == ' ' && rx_idx == 0) {
        // 跳过 ">>" 后的空格
      } else {
        // 提取数字字符（最多7位：XXX.XX）
        if (rx_idx < 7) {
          rx_voltage_str[rx_idx++] = ch;
        }
      }
    } else {
      rx_state = 0; // 不匹配则重置
    }
  }
  // 不处理TI，让UART_SendChar的轮询方式正常工作
}

void main(void) {
  // 初始化串口用于调试
  Uart1_Init();
  ES = 1; // 使能UART中断（被动接收STM32自动数据）
  EA = 1; // 使能总中断
  UART_SendString("[DEBUG] System Booting...\r\n");
  Display_Init();
  UART_SendString("[DEBUG] Display initialized\r\n");

  // 初始化显示
  Display_BootMessage();

  UART_SendString("[DEBUG] FakeLoad initialized\r\n");
  FakeLoad_Init();
  //	FakeLoad_Reset();
  FakeLoadTest();
  EC11_Init();
  UART_SendString("[DEBUG] EC11 encoder initialized\r\n");

  Timer0_Init();
  sys_tick_1ms = 0; // 硬件滴答计数器归零
  Key_Init();
  STM32_Init();
  UART_SendString("[DEBUG] STM32 ADC Helper initialized\r\n");

  // 不再阻塞等待STM32回复，由UART中断被动接收自动数据
  // 初始voltage=0，voltage_valid=0，屏幕显示"V:UK"

  // 启用风扇
  shellFAN(1);
  extFAN(1);
  // TLCx543_PinTest(); // 引脚测试
  // 显示空闲
  Display_IdleMessage();

  // 获取编码器旋转事件计数（用于调试）
  UART_SendString("[DEBUG] Encoder int count: ");
  sprintf(buffer, "%d", enc_int_count);
  UART_SendString(buffer);
  UART_SendString("\r\n");

  UART_SendString("[DEBUG] All systems initialized\r\n");

  while (1) {
    // 10ms基础节拍必执行
    // 自动接收STM32电压数据（中断方式，非阻塞）
    {
      if (voltage_updated) {
        float idata new_voltage;
        unsigned char idata i;
        unsigned int idata int_part;
        unsigned int idata frac_part;
        unsigned int idata frac_div;
        float idata diff;

        voltage_updated = 0;
        new_voltage = 0.0f;
        i = 0;
        int_part = 0;
        frac_part = 0;
        frac_div = 1;

        // 解析整数部分
        while (rx_voltage_str[i] >= '0' && rx_voltage_str[i] <= '9') {
          int_part = int_part * 10 + (rx_voltage_str[i] - '0');
          i++;
        }
        // 解析小数部分
        if (rx_voltage_str[i] == '.') {
          i++;
          while (rx_voltage_str[i] >= '0' && rx_voltage_str[i] <= '9') {
            frac_part = frac_part * 10 + (rx_voltage_str[i] - '0');
            frac_div *= 10;
            i++;
          }
        }
        new_voltage = (float)int_part + (float)frac_part / (float)frac_div;

        // === 电压变化检测：仅当变化超过阈值时才更新 ===
        diff = new_voltage - prev_voltage;
        if (diff < 0)
          diff = -diff;
        if (diff > 0.01f || prev_voltage < 0) {
          prev_voltage = new_voltage;
          voltage = new_voltage;
          voltage_valid = 1;
          // 电压变化时刷新屏幕（仅设置页面，空闲页面不覆盖）
          if (!is_running && !is_adjusting_duration_time && !is_idle) {
            power = FakeLoad_getPower(loadIndex, voltage);
            Display_FakeLoad(power, FakeLoad_getResistance(loadIndex), voltage);
          }
          sprintf(buffer, "V=%.2f\r\n", voltage);
          UART_SendString(buffer);
        }
        auto_rx_timeout = 0;
      } else {
        // 自动接收超时（约2秒无数据），使用主动查询后备
        auto_rx_timeout++;
        if (auto_rx_timeout >= 200) {
          auto_rx_timeout = 0;
          ES = 0; // 临时关闭UART中断，避免与轮询模式冲突
          {
            float idata fallback_v;
            float idata diff;

            fallback_v = STM32_ReadVoltage();
            if (fallback_v >= 0.0f) {
              diff = fallback_v - prev_voltage;
              if (diff < 0)
                diff = -diff;
              if (diff > 0.01f || prev_voltage < 0) {
                prev_voltage = fallback_v;
                voltage = fallback_v;
                voltage_valid = 1;
                // 电压变化时刷新屏幕（仅设置页面，空闲页面不覆盖）
                if (!is_running && !is_adjusting_duration_time && !is_idle) {
                  power = FakeLoad_getPower(loadIndex, voltage);
                  Display_FakeLoad(power, FakeLoad_getResistance(loadIndex),
                                   voltage);
                }
                sprintf(buffer, "V=%.2f\r\n", voltage);
                UART_SendString(buffer);
              }
            }
          }
          ES = 1; // 恢复UART中断
        }
      }
    }

    // 浮空检测：ADC读数为0或接近1023时，可能是输入未连接
    // {
    //   uint16_t idata raw_adc = TLCx543_ReadADC(6);
    //   if (raw_adc < 5 || raw_adc > 1020) {
    //     if (!prev_over_voltage) {
    //       Display_ErrorMessage("No Input");
    //       prev_over_voltage = 1;
    //     }
    //   } else {
    //     // 仅在非浮空状态清除错误标记
    //     // prev_over_voltage 由下面的过压检测独立管理
    //   }
    // }

    // === 编码器事件优先处理（避免被阻塞延迟） ===
    {
      unsigned char dir, key;
      if (EC11_GetEvent(&dir, &key)) {
        idle_counter = 0; // 重置空闲超时
        is_idle = 0;      // 退出空闲状态
        encode_CallBack((bit)dir, (bit)key);
      }
    }

    // 编码器按键非阻塞扫描
    if (EC11_ScanKey()) {
      idle_counter = 0; // 按键也重置空闲超时
      is_idle = 0;
    }

    // === 启停按键非阻塞处理（带状态机消抖） ===
    {
      static bit key_handled = 0; // 防止重复触发

      if (Key_Scan() == 0) {
        if (!key_handled) {
          start_key_debounce++;
          if (start_key_debounce >= 5) { // 50ms防抖
            start_key_debounce = 0;
            key_handled = 1;
            idle_counter = 0; // 重置空闲超时
            is_idle = 0;      // 退出空闲状态

            if (is_running) {
              // 运行中按下run键 → 停止运行，显示设置页面
              is_running = 0;
              buzzer_tick = 0;
              power = FakeLoad_getPower(loadIndex, voltage);
              Display_FakeLoad(power, FakeLoad_getResistance(loadIndex),
                               voltage);
              UART_SendString("[DEBUG] Stopped\r\n");
            } else if (duration_time_seconds > 0) {
              // 非运行且倒计时>0 → 开始运行
              is_running = 1;
              last_run_tick = sys_tick_1ms;
              UART_SendString("[DEBUG] Started\r\n");
            } else {
              // 非运行且倒计时=0 → 不动作，仅显示设置页面
              power = FakeLoad_getPower(loadIndex, voltage);
              Display_FakeLoad(power, FakeLoad_getResistance(loadIndex),
                               voltage);
              UART_SendString("[DEBUG] Timer is 0, stay in settings\r\n");
            }
          }
        }
      } else {
        start_key_debounce = 0;
        key_handled = 0; // 按键释放后才允许下次触发
      }
    }

    if (is_running) {
      heartFAN(1);
      runStateLED(1);
      // 运行指示灯
      P22 = 0;

      // 仅在倒计时运行时设置负载（倒计时结束后保持断开）
      if (duration_time_seconds > 0) {
        if (loadIndex != last_loadIndex_run) {
          FakeLoad_SetResistance(loadIndex);
          last_loadIndex_run = loadIndex;
        }
      }

      power = FakeLoad_getPower(loadIndex, voltage);

      // 使用sys_tick_1ms实现精确1秒刷新（不受主循环阻塞影响）
      if ((unsigned int)(sys_tick_1ms - last_run_tick) >= 1000) {
        last_run_tick = sys_tick_1ms;

        // 先减后显示：确保倒计时结束时显示00:00而非00:01
        if (duration_time_seconds > 0) {
          duration_time_seconds--;
          if (duration_time_seconds == 0) {
            buzzer_tick = BEEP_SEC * 100;
            FakeLoad_SetPower(0, voltage);
            // 不清除is_running：保持显示00:00等待用户按run键确认
          }
        }

        // 倒计时结束后仍显示00:00 Running，直到用户按run键退出
        Display_RunningMessage(duration_time_seconds, power, voltage);
      }
    } else {
      heartFAN(0);
      runStateLED(0);
      // 停止时复位负载，重置运行模式变量
      FakeLoad_Reset();
      // 运行指示灯
      P22 = 1;
      last_loadIndex_run = 0xFF; // 强制下次运行时重新设置电阻
      last_run_tick = 0;         // 重置计时基准
      // 不重置buzzer_tick：让蜂鸣器自然响完
    }

    // 安全检查：电压过高时显示错误信息（仅电压超限时显示一次）
    if (voltage > SAFE_VOLTAGE) {
      if (!prev_over_voltage) {
        Display_ErrorMessage("Over Voltage");
        prev_over_voltage = 1;
      }
    } else {
      prev_over_voltage = 0;
    }

    // === 空闲超时检测（1分钟无操作进入空闲屏幕） ===
    if (!is_running && idle_counter < IDLE_TIMEOUT_TICKS) {
      idle_counter++;
      if (idle_counter >= IDLE_TIMEOUT_TICKS && !is_idle) {
        is_idle = 1;
        Display_IdleMessage();
      }
    }

    // 蜂鸣器逻辑
    if (buzzer_tick > 0) {
      BuzzerPWM(200); // 鸣叫频率200，可自行调整音调
      buzzer_tick--;
    } else {
      BuzzerPWM(0); // 关闭蜂鸣
    }

    Delay_ms(10);
  }
}
// 编码器方向/按键状态处理函数（在主循环中调用）
void encode_CallBack(bit dir, bit keystate) {
  // keystate=1：编码器按键按下，切换调节模式
  if (keystate == 1) {
    is_adjusting_duration_time = !is_adjusting_duration_time;
  }

  if (is_adjusting_duration_time) {
    if (dir == 0) {
      if (duration_time_seconds < 7200) // 上限7200秒 2小时
        duration_time_seconds++;
    } else {
      if (duration_time_seconds > 0) // 禁止负数
        duration_time_seconds--;
    }
    Display_TimerSetupMessage(duration_time_seconds);
  } else {
    // 调整功率负载（电压由STM32自动更新，保留当前voltage值）

    if (dir == 0) {
      if (loadIndex < RESISTANCE_LIST_SIZE - 1) {
        loadIndex++;
      }
    } else {
      if (loadIndex > 0) {
        loadIndex--;
      }
    }
    power = FakeLoad_getPower(loadIndex, voltage);
    Display_FakeLoad(power, FakeLoad_getResistance(loadIndex), voltage);
  }
}

// INT1已禁用（EC11_Init中EX1=0），此中断函数不再使用
// void INT1_Isr(void) interrupt 2 {
//   EA = 0;
//   encFlag |= 0x01;
//   encKeyPress = 1;
//   EA = 1;
// }