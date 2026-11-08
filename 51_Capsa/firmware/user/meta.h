#ifndef __META_H__
#define __META_H__

/* =====================================================================
 * meta.h — 24C02 属性存储器（IIC2，地址 0x51，256 B）
 *
 * 布局（与 COMMAND_REFERENCE.md 第 3 节一致）：
 *   0x00      初始化标志      1 B   0x01 = 已初始化
 *   0x01      WP 掩码         1 B   bit0~7 ↔ 0x50~0x57，1 = 写保护
 *   0x02      健康掩码        1 B
 *   0x03~16   名称           20 B   UTF-8 / ASCII，'\0' 结尾
 *   0x17~18   阵列容量 KB     2 B   LE
 *   0x19~1C   修改时间        4 B   LE（Unix 秒）
 *   0x1D~20   读取时间        4 B   LE
 *   0x21      介质            1 B   0 EEPROM / 1 FRAM / 2 EERAM / 3 SRAM
 *   0x22~25   型号 Kbit       4 B   LE
 *   0x26~2B   UID             6 B
 *   0x2C~31   已使用空间      6 B   LE（uint48，本固件按 uint32 处理）
 *   0x32~39   数据哈希        8 B
 *   0x3A~3D   累计写入次数    4 B   LE
 *   0x3E~F5   预留
 *   0xF6~FF   初始化检测区   10 B   全 0xFF = 未初始化
 *
 * ★ 检测区是"是否初始化"的唯一判据；DI 区（0x32~0x3D）在初始化时只在
 *   出厂空白态（整段 0xFF 或 0x00）才被规范成 0，已有寿命计数绝不抹掉。
 * ===================================================================== */

#include "board.h"

#define META_INIT_FLAG          0x00
#define META_WP_MASK            0x01
#define META_HEALTH_MASK        0x02
#define META_NAME               0x03
#define META_NAME_LEN           20
#define META_CAP_KB             0x17
#define META_MTIME              0x19
#define META_RTIME              0x1D
#define META_MEDIUM             0x21
#define META_MODEL              0x22
#define META_UID                0x26
#define META_UID_LEN            6
#define META_USED               0x2C
#define META_USED_LEN           6
#define META_HASH               0x32
#define META_HASH_LEN           8
#define META_WRITE_COUNT        0x3A
#define META_WC_LEN             4
#define META_RESERVED           0x3E
#define META_DETECT             0xF6
#define META_DETECT_LEN         10

typedef struct
{
    uint8_t  init_flag;
    uint8_t  wp_mask;
    uint8_t  health_mask;
    char     name[META_NAME_LEN + 1];
    uint16_t cap_kb;
    uint32_t mtime;
    uint32_t rtime;
    uint8_t  medium;
    uint32_t model_kbit;
    uint8_t  uid[META_UID_LEN];
    uint32_t used;
    uint8_t  hash[META_HASH_LEN];
    uint32_t write_count;
} meta_t;

extern meta_t xdata g_meta;

/* 上电：检测区判定 + 读入缓存。返回 1 = 已初始化 */
bit  Meta_Load(void);
bit  Meta_IsInitialized(void);

/* ★ 属性数据"可展示"判定（比 Meta_IsInitialized 更严）：
 *   检测区非全 0xFF  &&  init_flag == 0x01  &&
 *   medium <= 3       &&  model_kbit 属于已知型号  &&
 *   cap_kb 不是 0 / 0xFFFF。
 *   只有它成立，屏幕才允许显示容量、型号这些"存储卡属性"。
 *   半初始化、擦到一半、从没写过的卡一律为 0（显示未初始化画面）。*/
bit  Meta_InfoValid(void);

/* ★ 有效容量（字节）= 可写片数 × 单片容量。
 *   写保护片写不进去，不能算进可用容量 —— 屏幕上的容量行、UG 的上限、
 *   cap_kb 字段全部以它为准。以前用"8 片 × 单片"，WP 后显示的容量是虚的。 */
uint32_t Meta_ArrayBytes(void);

/* ★ 按当前型号 + WP 掩码重算 g_meta.cap_kb（有效容量口径）。
 *   handle_set 在任何 WP 掩码变化后都必须调用它 —— 上位机点击 WP 徽章
 *   发的 SET 其余字段全是 '!' 占位，只有重算这一条路能把容量跟着改掉，
 *   然后随 Meta_SaveInfo / Meta_FirstSet 一起写进 24C02。 */
void Meta_RecalcCapacity(void);

/* 首次初始化：清 0x00~0x2B / 0x3E~0xF5，DI 区仅空白时规范成 0 */
bit  Meta_FirstSet(void);

/* 字段级写回（只写变化的段，减少 24C02 磨损）*/
bit  Meta_SaveInfo(void);           /* 0x00~0x2B 整段（名称/容量/时间/介质/型号/UID）*/
bit  Meta_SaveUsed(void);           /* 0x2C~0x31 */
bit  Meta_SaveDataInfo(void);       /* 0x32~0x3D */
bit  Meta_SaveDetect(void);         /* 0xF6~0xFF 写 0x00 */

/* 便捷读写 */
void Meta_SetUsed(uint32_t used);
void Meta_SetDataInfo(bit has_hash, const uint8_t *hash8, bit has_count, uint32_t count);

#endif /* __META_H__ */
