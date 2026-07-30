#include "key.h"
#include "uart.h"

static uint16_t xdata Reset_Cnt = 0;
static bit last_reset_state = 1;
static uint8_t  xdata debounce_cnt = 0;  

void Key_Init(void){
    IT1 = 1;           // INT1(P3.3)下降沿触发
    EX1 = 1;           // 使能INT1中断
    Mode_Key  = 1;
    Reset_Key = 1;
}

bit Key_Scan(void){
    bit cur = Reset_Key;
    
    /* 调试: 打印按键状态变化 */
    if (cur != last_reset_state) {
        if (cur == 0)
            DEBUG_UO("Key", "Reset pressed");
        else
            DEBUG_UO("Key", "Reset released");
        last_reset_state = cur;
    }
    
    if(cur == 0){
        /* 去抖: 连续采样到低电平达到3次(≈30ms)后才开始计数 */
        if (debounce_cnt < 3) {
            debounce_cnt++;
        } else {
            if(Reset_Cnt < 0xFFFF) Reset_Cnt++;
        }
    } else {
        Reset_Cnt = 0;
        debounce_cnt = 0;
    }

    if(Reset_Cnt >= KEY_LONGPRESS_CYCLES){
        Reset_Cnt = 0;
        DEBUG_UO("Key", "Long press DETECTED!");
        return 1;
    }
    return 0;
}