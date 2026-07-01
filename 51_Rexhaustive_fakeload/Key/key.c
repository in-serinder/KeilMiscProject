#include "key.h"

// 只能通过扫描键盘来获取按键状态
void Key_Init(void) { RunKey = 1; }

bit Key_Scan(void) { return RunKey; }
