/**
 * @file    main.c
 * @brief   自动量程电压测量仪主程序
 *
 * 业务逻辑说明：
 *   Step1 - 开机初始化所有硬件，继电器默认 NC 状态 = 600V 挡位
 *   Step2 - 通过 800V 侦察通道检测电压，自动路由到合适挡位
 *   常驻任务：
 *     - 间插读取指定挡位电压 | 间插读取800V侦察通道做安全切换
 *     - 基于800V侦察电压做多周期消抖拨挡（滞回判断）
 *     - 电压突变安全机制（浮动阈值快速切换）
 *     - 记录峰值/谷值并显示
 *     - 每周期结束绿灯闪烁
 *
 * 周期 = 1s，工作流：
 *   间插ADC读取 → 安全切换/消抖判断 → 整理峰值谷值 → 显示 → 绿灯闪烁
 */
#include "ADC.h"
#include "Channel_Ctl.h"
#include "Delay.h"
#include "Display.h"
#include "LED.h"
#include "MAX7219.h"
#include "TM1637.h"
#include "UART.h"
#include "stm32f10x.h"

/* ==================================================================
 * 宏定义
 * ================================================================== */

/** 挡位编码：0=5V, 1=36V, 2=250V, 3=600V */
#define CH_CODE_5V 0
#define CH_CODE_36V 1
#define CH_CODE_250V 2
#define CH_CODE_600V 3

/** 电压门限（伏） */
#define THRESH_5V_UP 5.0     /**< 5V→36V 升挡门限 */
#define THRESH_36V_UP 36.0   /**< 36V→250V 升挡门限 */
#define THRESH_250V_UP 250.0 /**< 250V→600V 升挡门限 */
#define THRESH_250V_DN 225.0 /**< 600V→250V 降挡门限 */
#define THRESH_36V_DN 32.0   /**< 250V→36V 降挡门限 */
#define THRESH_5V_DN 4.5     /**< 36V→5V 降挡门限 */

/** 电压突变浮动阈值（伏），超过此值视为剧变，立即切换 */
#define JITTER_THRESHOLD 0.8

/** 消抖周期数：电压连续 N 个周期稳定在目标挡位范围内才切换 */
#define DEBOUNCE_CYCLES 3

/** 挡位锁定周期数（切换后锁定，防止来回抖动） */
#define LOCK_CYCLES 5

/** 每个周期内对侦察通道的采样次数（间插在挡位采样中一起做） */
#define SCOUT_SAMPLES 20

/** 每个周期内对指定挡位的采样次数 */
#define CHAN_SAMPLES 20

/** 滑动窗口大小（用于消抖平均） */
#define WINDOW_SIZE 10

/** ADC 参考电压 */
#define VREF 3.3
#define ADC_MAX 4095.0

/** 800V 通道分压比因子 = (R1+R2)/R2 */
#define DIV_800V_FACTOR ((2400000.0 + 10000.0) / 10000.0)

/* ==================================================================
 * 挡位编码 ↔ 实际电压值 转换表
 * ================================================================== */

/**
 * @brief 挡位编码转通道电压标称值
 */
static uint16_t CodeToVoltage(uint8_t code) {
  static const uint16_t table[] = {5, 36, 250, 600};
  return table[code];
}

/**
 * @brief 挡位编码执行继电器切换
 */
static void CodeSwitchChannel(uint8_t code) {
  switch (code) {
  case CH_CODE_5V:
    ChannelSet_5V();
    break;
  case CH_CODE_36V:
    ChannelSet_36V();
    break;
  case CH_CODE_250V:
    ChannelSet_250V();
    break;
  case CH_CODE_600V:
    ChannelSet_600V();
    break;
  default:
    ChannelSet_600V();
    break;
  }
}

/**
 * @brief 根据 800V 侦察电压判断目标挡位编码
 *
 * 带滞回防止临界抖动:
 *   - 升挡用较高的门限
 *   - 降挡用较低的门限
 */
static uint8_t ScoutToTargetCode(double scout_v, uint8_t current_code) {
  uint16_t cur_v = CodeToVoltage(current_code);

  if (current_code < CH_CODE_600V && scout_v >= THRESH_250V_UP) {
    return CH_CODE_600V;
  }
  if (current_code < CH_CODE_250V && scout_v >= THRESH_36V_UP) {
    return CH_CODE_250V;
  }
  if (current_code < CH_CODE_36V && scout_v >= THRESH_5V_UP) {
    return CH_CODE_36V;
  }

  if (current_code > CH_CODE_5V && scout_v < THRESH_5V_DN) {
    return CH_CODE_5V;
  }
  if (current_code > CH_CODE_36V && scout_v < THRESH_36V_DN) {
    return CH_CODE_36V;
  }
  if (current_code > CH_CODE_250V && scout_v < THRESH_250V_DN) {
    return CH_CODE_250V;
  }

  return current_code; /* 未超门限，保持当前挡位 */
}

/**
 * @brief 读取指定挡位的实际电压值
 */
static double ReadChannelVoltage(uint8_t code) {
  switch (code) {
  case CH_CODE_600V:
    return ADC_Get600V();
  case CH_CODE_250V:
    return ADC_Get240V();
  case CH_CODE_36V:
    return ADC_Get36V();
  case CH_CODE_5V:
    return ADC_Get5V();
  default:
    return ADC_Get800V();
  }
}

/* ==================================================================
 * 主函数
 * ================================================================== */
int main(void) {
  /* ---- 局部变量 ---- */
  uint8_t i;
  uint8_t current_code;         /* 当前挡位编码 */
  uint16_t current_ch;          /* 当前挡位标称电压 */
  uint8_t target_code;          /* 消抖后目标挡位编码 */
  uint8_t debounce_counter = 0; /* 消抖计数器 */
  uint8_t lock_counter = 0;     /* 切换锁定计数器 */

  double scout_v;          /* 单次 800V 侦察值 */
  double scout_acc;        /* 周期内侦察 ADC 累加值 */
  double scout_avg;        /* 周期内侦察平均值 */
  double last_scout = 0.0; /* 上一周期侦察平均值 */
  double scout_change;     /* 侦察变化量 */

  double ch_voltage;        /* 当前挡位单次采样值 */
  double ch_peak = 0.0;     /* 当前周期峰值 */
  double ch_valley = 1.0e9; /* 当前周期谷值 */
  double ch_acc;            /* 当前挡位采样累加值 */

  double window_buf[WINDOW_SIZE]; /* 滑动窗口 */
  uint8_t window_idx = 0;
  double window_sum = 0.0;

  double display_peak = 0.0;   /* 上次显示的峰值（用于去冗余刷新） */
  double display_valley = 0.0; /* 上次显示的谷值 */

  uint32_t tick_5s = 0;     /* 5秒周期计数器 */
  uint8_t fast_switch_flag; /* 本周期是否发生了快速切换 */

  /* ==============================================================
   * Step 1: 硬件初始化
   * ============================================================== */
  UART_Init();
  UART_SendString("\r\n========== System Start ==========\r\n");

  LED_Init();
  Display_Init();    /* 内部调用 MAX7219_Init + TM1637_Init */
  ChannelSet_Init(); /* 默认所有继电器 NC → 600V 挡位 */
  VoltageADC_Init();

  /* 初始挡位 = 600V（NC 默认状态） */
  current_code = CH_CODE_600V;
  current_ch = 600;

  /* ==============================================================
   * Step 2: 通过 800V 侦察通道检测，自动路由到合适挡位
   * ============================================================== */
  LED_GREEN_On();
  scout_acc = 0.0;
  for (i = 0; i < 5; i++) {
    scout_acc += ADC_Get800V();
    Delay_ms(5);
  }
  scout_avg = scout_acc / 5.0;
  LED_GREEN_Off();

  target_code = ScoutToTargetCode(scout_avg, current_code);
  if (target_code != current_code) {
    current_code = target_code;
    current_ch = CodeToVoltage(current_code);
    CodeSwitchChannel(current_code);
    Delay_ms(20); /* 继电器稳定延时 */
  }
  lock_counter = LOCK_CYCLES; /* 开机切换后锁定 */

  /* 初始化滑动窗口 */
  for (i = 0; i < WINDOW_SIZE; i++) {
    window_buf[i] = scout_avg;
  }
  window_sum = scout_avg * WINDOW_SIZE;

  /* 自检 + 显示当前通道 */
  Display_QuickSelfTest(current_ch);
  Display_NoInput(); /* 自检后立即显示 "NON.V" 占位，避免黑屏 */
  Display_Channel(current_ch);

  /* 初始化显示缓存 */
  display_peak = -1.0; /* 初始化为负值，确保第一次进入主循环一定会刷新 */
  display_valley = -1.0;

  UART_Printf("[INIT] Scout=%.2fV, Channel=%dV\r\n", scout_avg, current_ch);

  /* ==============================================================
   * 主循环（每个周期 ≈ 1 秒）
   * ============================================================== */
  while (1) {
    fast_switch_flag = 0;
    ch_peak = 0.0;
    ch_valley = 1.0e9;
    ch_acc = 0.0;
    scout_acc = 0.0;

    /* ------------------------------------------------------------
     * 间插采样：交替读取【当前挡位电压】和【800V 侦察电压】
     *
     * 顺序：挡位1 → 侦察1 → 挡位2 → 侦察2 → ...
     * 这样在 1 秒周期内，两个通道的采样均匀分布，实时性更好
     *
     * 后半部分（最后几个采样点）专门用于快速安全切换判断
     * ---------------------------------------------------------- */

    /* 第一阶段：正常间插采样（挡位 + 侦察交错） */
    for (i = 0; i < CHAN_SAMPLES && i < SCOUT_SAMPLES; i++) {
      /* 读当前挡位电压 */
      ch_voltage = ReadChannelVoltage(current_code);
      ch_acc += ch_voltage;
      if (ch_voltage > ch_peak)
        ch_peak = ch_voltage;
      if (ch_voltage < ch_valley)
        ch_valley = ch_voltage;

      /* 读 800V 侦察电压 */
      scout_v = ADC_Get800V();
      scout_acc += scout_v;

      /* 安全检查：突变检测 —— 与上次侦察值比较 */
      scout_change = scout_v - last_scout;
      if (scout_change < 0)
        scout_change = -scout_change;

      if (scout_change > JITTER_THRESHOLD) {
        /* 电压剧变，立即重新路由 */
        target_code = ScoutToTargetCode(scout_v, current_code);
        if (target_code != current_code) {
          current_code = target_code;
          current_ch = CodeToVoltage(current_code);
          CodeSwitchChannel(current_code);
          Delay_ms(20);

          /* 切换后重置内部状态 */
          lock_counter = LOCK_CYCLES;
          debounce_counter = 0;
          fast_switch_flag = 1;

          UART_Printf("[SAFETY_JMP] Scout=%.2fV, Channel=%dV\r\n", scout_v,
                      current_ch);

          /* 切换后立即重新初始化本周期采样值 */
          ch_peak = ReadChannelVoltage(current_code);
          ch_valley = ch_peak;
          ch_acc = ch_peak;
          break; /* 退出采样循环，进入整理阶段 */
        }
      }
    }

    /* 第二阶段：如果未发生切换，继续完成剩余采样 */
    if (!fast_switch_flag) {
      uint8_t remaining =
          (CHAN_SAMPLES > SCOUT_SAMPLES) ? CHAN_SAMPLES : SCOUT_SAMPLES;

      for (; i < remaining; i++) {
        if (i < CHAN_SAMPLES) {
          ch_voltage = ReadChannelVoltage(current_code);
          ch_acc += ch_voltage;
          if (ch_voltage > ch_peak)
            ch_peak = ch_voltage;
          if (ch_voltage < ch_valley)
            ch_valley = ch_voltage;
        }
        if (i < SCOUT_SAMPLES) {
          scout_acc += ADC_Get800V();
        }
      }
    }

    /* 计算周期内侦察平均值 */
    scout_avg = scout_acc / SCOUT_SAMPLES;

    /* 更新滑动窗口 */
    window_sum -= window_buf[window_idx];
    window_buf[window_idx] = scout_avg;
    window_sum += scout_avg;
    window_idx = (window_idx + 1) % WINDOW_SIZE;
    double window_avg = window_sum / WINDOW_SIZE;

    /* ------------------------------------------------------------
     * 消抖拨挡逻辑
     * ------------------------------------------------------------
     *
     * 原理：
     *   1. 用滑动平均 window_avg 判断目标挡位
     *   2. 如果目标 != 当前，debounce_counter++，连续 N 次才切换
     *   3. 如果目标 == 当前，debounce_counter 归零
     *   4. 切换后进入锁定期，期间不执行消抖判断
     */
    if (!fast_switch_flag && lock_counter == 0) {
      target_code = ScoutToTargetCode(window_avg, current_code);

      if (target_code != current_code) {
        debounce_counter++;
        if (debounce_counter >= DEBOUNCE_CYCLES) {
          /* 稳定超过 N 个周期，执行切换 */
          current_code = target_code;
          current_ch = CodeToVoltage(current_code);
          CodeSwitchChannel(current_code);
          Delay_ms(20);
          lock_counter = LOCK_CYCLES;
          debounce_counter = 0;

          UART_Printf("[DEBOUNCE] Avg=%.2fV, Channel=%dV\r\n", window_avg,
                      current_ch);
        }
      } else {
        debounce_counter = 0;
      }
    }

    /* 锁定倒计时 */
    if (lock_counter > 0) {
      lock_counter--;
    }

    /* 更新上一周期侦察值 */
    last_scout = scout_avg;

    /* ------------------------------------------------------------
     * 显示更新（去冗余刷新）
     * ---------------------------------------------------------- */
    double diff_p = ch_peak - display_peak;
    if (diff_p < 0)
      diff_p = -diff_p;
    double diff_v = ch_valley - display_valley;
    if (diff_v < 0)
      diff_v = -diff_v;

    if (diff_p > 0.05 || diff_v > 0.05) {
      Display_PeakVoltage(ch_peak, ch_valley);
      display_peak = ch_peak;
      display_valley = ch_valley;
    }
    Display_Channel(current_ch);

    /* ------------------------------------------------------------
     * 定时维护：每 5 个周期（≈5秒）重新初始化 MAX7219 配置
     * 防止偶发 SPI 通讯错乱导致显示熄灭
     * ---------------------------------------------------------- */
    tick_5s++;
    if (tick_5s >= 5) {
      tick_5s = 0;
      MAX7219_ReInit();
      Display_PeakVoltage(ch_peak, ch_valley);
      UART_PrintAllADC();
    }

    /* ------------------------------------------------------------
     * 周期结束：绿灯闪烁提示
     * ---------------------------------------------------------- */
    LED_GREEN_On();
    Delay_ms(50);
    LED_GREEN_Off();

    /* 等待完成 1 秒周期（扣除已消耗时间） */
    Delay_ms(950);
  }
}
