#ifndef __BOARD_H__
#define __BOARD_H__

/* =====================================================================
 * board.h  —  板级配置（引脚 / 时钟 / 业务参数 / 编译开关）
 *
 * MCU : STC8G1K08A-8PIN  (1T 内核，22.1184MHz)
 *
 * 8 脚封装的 IO 就这 6 个，全部用满：
 *   P3.0 / P3.1  空（UART1，本工程未用，需要调试时可放开）
 *   P3.2  ADC2   MQ 传感器模拟输出（高阻输入）
 *   P3.3         有源蜂鸣器，高电平驱动（推挽输出）
 *   P5.4  CLK    TM1637 时钟
 *   P5.5  DIO    TM1637 数据
 *
 * ★ P5.4 出厂是 RST 脚：必须在 STC-ISP 下载时勾选
 *   "复位脚用作 I/O 口"，否则数码管全黑（CLK 一直被拉在复位电平上）。
 *
 * 按键：add/sub 接在数码管模块的 A-K1 / A-K2 上，由 TM1637 片内键扫
 *       读出（42H 命令），所以不额外占用 IO。见 hardware/key.c。
 *
 * 换板子只改本文件，其它源文件不要出现硬编码引脚。
 * ===================================================================== */

#include "stc8g1k.h"
#include <intrins.h>

/* ---------------- 系统时钟 ----------------
 * 必须与 STC-ISP 里选的 IRC 频率一致（22.1184MHz 是 115200 波特率的整数倍）。
 * 不一致时：延时、TM1637 时序、ADC 速度全部按比例偏。 */
#define FOSC_HZ                 22118400UL

/* ---------------- TM1637 四位数码管：P5.4=CLK  P5.5=DIO ---------------- */
#define TM1637_CLK              P54
#define TM1637_DIO              P55

/* ---------------- 有源蜂鸣器：P3.3，高电平响 ---------------- */
#define BEEP                    P33

/* ---------------- MQ 传感器输入 ----------------
 * ★★★ 先确认片子型号！手册 1.4 产品线表：
 *      STC8G1K08-8Pin（无 A）  —— 6 个 I/O，**没有 ADC**
 *      STC8G1K08A（含 SOP8）   —— 6 个 I/O，**6 通道 10 位 ADC**
 *   带 A 的：通道 0~5 = P3.0/P3.1/P3.2/P3.3/P5.4/P5.5，通道 15 = 内部 1.19V。
 *   不带 A 的：ADC_CONTR(0xBC)/ADCCFG(0xDE)/ADCTIM 这些 SFR 根本不存在，
 *              写进去读回来是 0，转换永远不完成 —— 表现为数码管一直显示 Err。
 *   程序开机回读 ADC_CONTR/ADCCFG 自动判断，没有 ADC 就显示 "noAd"。 */
#define MQ_ADC_CH               2
#define MQ_ADC_PIN              P32

/* 1 = 不用 ADC，改读 MQ 模块的比较器数字输出 DO（LM393，常见模块都有）
 *     阈值由模块上的蓝色电位器决定，本机只做"报警/不报警"和消抖；
 *     数码管显示 1023（报警态）/ 0。没有带 A 的片子时可以先用这个把功能跑通。 */
#ifndef CFG_MQ_DIGITAL
#define CFG_MQ_DIGITAL  0
#endif
/* 1 = 开机自检发现 ADC 转不动时，自动切到数字 DO 模式（不显示 noAd，直接可用）。
 *     你的板子当前就是这种情况：ADC 那组 SFR 读回 0（片子/批次问题），
 *     自动切换后只要把模块 DO 接到 P3.2 就能跑，阈值由模块电位器定。 */
#ifndef CFG_MQ_AUTO_DO
#define CFG_MQ_AUTO_DO  1
#endif

/* 数字模式下 DO 的电平：1 = 检测到气体时 DO 为低（多数模块如此） */
#ifndef CFG_MQ_DO_ACTIVE_LOW
#define CFG_MQ_DO_ACTIVE_LOW    1
#endif

/* ADC 转换超时的空转次数上限（正常 30~60us 就置位，这里给到约 4ms@22MHz） */
#define CFG_ADC_GUARD           8000

/* ---------------- 业务参数 ---------------- */
#define CFG_TH_DEFAULT          400     /* 首次上电/EEPROM 无效时的阈值（0~1023） */
#define CFG_TH_STEP             5       /* 每按一次加减的步长 */
#define CFG_TH_MAX              1023    /* 10 位 ADC 满量程 */
#define CFG_TH_HYST             12      /* 报警回差，防止阈值附近抖动导致蜂鸣器哮喘 */
#define CFG_SET_TIMEOUT_MS      3000    /* 进入设置模式后无操作的保持时间 */
#define CFG_BLINK_MS            250     /* 设置模式下闪烁的半周期 */

/* ---------------- 片内 EEPROM（用 Flash 模拟）----------------
 * ★ 手册 15.3：STC8G1K08 / 1K08A 的 EEPROM 是独立的 4K，
 *   IAP 地址只有 0000h~0FFFh（8 个 512B 扇区），程序区 8K 在另一块空间。
 *   所以参数地址必须落在 0x0FFF 以内 —— 早先填 0x1C00（程序区末尾）是无效地址，
 *   IAP 会直接失败（IAP_CONTR.CMD_FAIL=1），回读全 FF，表现就是"存不住"。
 * 这里用最后一个扇区 0x0E00~0x0FFF，前面留给以后扩展。
 * 该型号 EEPROM 大小固定 4K，不需要在 STC-ISP 里另外设置。 */
#define CFG_EEPROM_BASE         0x0E00

/* ---------------- 编译开关 ---------------- */
/* 按键取法（详见 hardware/key.c）：
 *   0 = TM1637 片内键扫（A 接在模块内部某条 SEG 上）—— 默认，最省 IO
 *   1 = GPIO 直读（模块的 A 其实是 GND，K1/K2 引到 P3.0/P3.1，按下拉低）
 * 上板后若 add/sub 完全没反应，先把本项改 1 试试（两种接法都不需加硬件）。 */
#ifndef CFG_KEY_GPIO_MODE
#define CFG_KEY_GPIO_MODE 0
#endif

#if CFG_KEY_GPIO_MODE
#define KEY_ADD_PIN             P30
#define KEY_SUB_PIN             P31
#endif

/* 键扫读取函数是否参与编译（GPIO 直读时用不到，省掉免得链接器报 L16） */
#define CFG_TM1637_KEYSCAN      (!CFG_KEY_GPIO_MODE)

/* 1 = add/sub 与模块上的 K1/K2 反接时交换一下。
 * 实测本板：丝印 add 的键读回 FFF7（K2 行）、丝印 sub 的键读回 FFEF（K1 行），
 * 所以要交换。若你的模块是"add=K1、sub=K2"，把它改回 0。 */
#ifndef CFG_KEY_SWAP
#define CFG_KEY_SWAP    1
#endif

/* 1 = 键位自检：按键后数码管显示 42H 读回的原始键值（16 进制，低字节才是键码）。
 *     正常应为 add=0x??EF、sub=0x??F7（?? 是第二个字节，一般 FF）。
 *     若两个键显示完全一样、或一直是 FFFF，说明模块不是片内键扫接法，
 *     改用上面的 CFG_KEY_GPIO_MODE = 1。 */
#ifndef CFG_KEY_RAW_SHOW
#define CFG_KEY_RAW_SHOW 0
#endif

/* 1 = 串口调试输出（P3.1=TXD，9600 8N1）。
 *     每秒一行运行状态，开机打一段自检信息（ADC 通道探测、寄存器回读）。
 *     注：GPIO 直读取键用的是 P3.0/P3.1，和串口抢引脚，二选一。 */
#ifndef CFG_DEBUG_UART
#define CFG_DEBUG_UART  1
#endif

#if CFG_KEY_GPIO_MODE && CFG_DEBUG_UART
#error "CFG_KEY_GPIO_MODE=1 占用 P3.0/P3.1，与串口调试冲突：请把 CFG_DEBUG_UART 改成 0"
#endif

void Board_Init(void);

#endif /* __BOARD_H__ */
