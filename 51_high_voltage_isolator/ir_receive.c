#include "ir_receive.h"
#include "uart.h"

// 红外接收相关变量
unsigned char ir_raw_data[4]; // 存储红外接收的原始数据，共4字节
bit ir_receive_complete = 0;   // 红外接收完成标志
unsigned char ir_key_code = 0;  // 解码后的按键编码

// 定时器0计数变量
unsigned int time_count = 0;
// 数据位计数
unsigned char bit_count = 0;
// 接收状态
unsigned char ir_state = 0;

// 当主进程获取后清除
uint8_t receive_flag =0 ;

// 按键编码映射表（根据实际遥控器调整）
// 这里使用常见的NEC协议遥控器按键编码
code unsigned char IR_KEY_MAP[] = {
    0x00, // 无按键
    0x16, // 0
    0x0C, // 1
    0x18, // 2
    0x5E, // 3
    0x08, // 4
    0x1C, // 5
    0x5A, // 6
    0x42, // 7
    0x52, // 8
    0x4A  // 9
};

// 初始化红外接收
void IR_Init(void)
{
    // 初始化INT1为下降沿触发
    IT1 = 1;
    // 开启INT1中断
    EX1 = 1;
    // 开启总中断
    EA = 1;

    // 初始化定时器0，用于测量脉冲宽度
    TMOD &= 0xF0;
    TMOD |= 0x01; // 定时器0工作在模式1
    TH0 = 0;
    TL0 = 0;
    TR0 = 0; // 先关闭定时器
}

// INT1中断处理函数，用于红外接收
void IR_ISR(void) interrupt 0
{
    switch (ir_state)
    {
    case 0:      // 初始状态，检测引导码
        TR0 = 1; // 启动定时器
        TH0 = 0;
        TL0 = 0;
        ir_state = 1;
        break;
    case 1: // 检测引导码低电平
        time_count = TH0 * 256 + TL0;
        TH0 = 0;
        TL0 = 0;
        // 引导码低电平约9ms，允许误差
        if (time_count > 8000 && time_count < 10000)
        {
            ir_state = 2;
        }
        else
        {
            ir_state = 0;
        }
        break;
    case 2: // 检测引导码高电平
        time_count = TH0 * 256 + TL0;
        TH0 = 0;
        TL0 = 0;
        // 引导码高电平约4.5ms，允许误差
        if (time_count > 4000 && time_count < 5000)
        {
            ir_state = 3;
            bit_count = 0;
        }
        else
        {
            ir_state = 0;
        }
        break;
    case 3: // 接收数据位
        time_count = TH0 * 256 + TL0;
        TH0 = 0;
        TL0 = 0;

        // 数据位低电平约560us
        if (time_count > 400 && time_count < 700)
        {
            ir_state = 4;
        }
        else
        {
            ir_state = 0;
        }
        break;
    case 4: // 检测数据位高电平
        time_count = TH0 * 256 + TL0;
        TH0 = 0;
        TL0 = 0;

        // 数据位高电平：0为560us，1为1680us
        if (time_count > 400 && time_count < 700) // 0
        {
            ir_raw_data[bit_count / 8] &= ~(0x80 >> (bit_count % 8));
        }
        else if (time_count > 1500 && time_count < 1900) // 1
        {
            ir_raw_data[bit_count / 8] |= (0x80 >> (bit_count % 8));
        }
        else
        {
            ir_state = 0;
            break;
        }

        bit_count++;
        if (bit_count >= 32) // 接收完32位数据
        {
            // 验证数据有效性（地址码与地址反码应互补）
            if ((ir_raw_data[0] ^ ir_raw_data[1]) == 0xFF)
            {
                // 验证数据码与数据反码应互补
                if ((ir_raw_data[2] ^ ir_raw_data[3]) == 0xFF)
                {
                    // 解码按键编码
                    ir_key_code = ir_raw_data[2];
                    ir_receive_complete = 1; // 设置接收完成标志
                }
            }
            ir_state = 0;
        }
        else
        {
            ir_state = 3;
        }
        break;
    }
}

// 获取解码后的按键编码
unsigned char IR_GetKeyCode(void)
{
    if (ir_receive_complete)
    {
        ir_receive_complete = 0; // 清除标志
        receive_flag = 1; // 设置接收完成标志
        return ir_key_code;
    }
    return 0; // 无按键
}

// 获取数字按键值（0-9）
unsigned char IR_GetDigit(void)
{
    unsigned char key_code = IR_GetKeyCode();
    unsigned char i;
    
    // 查找按键编码对应的数字
    for (i = 0; i < 11; i++)
    {
        if (IR_KEY_MAP[i] == key_code)
        {
            return i; // 返回数字（0-9）
        }
    }
    return 0xFF; // 非数字按键
}


uint8_t get_receive_flag(){
    return receive_flag;
}

void clear_receive_flag(){
    receive_flag = 0;
}

// 通过串口发送红外数据
void IR_SendData(void)
{
    if (ir_receive_complete)
    {
        UART_SendString("IR Data: ");
        UART_SendHex(ir_raw_data[0]); // 地址码
        UART_SendByte(' ');
        UART_SendHex(ir_raw_data[1]); // 地址反码
        UART_SendByte(' ');
        UART_SendHex(ir_raw_data[2]); // 数据码
        UART_SendByte(' ');
        UART_SendHex(ir_raw_data[3]); // 数据反码
        UART_SendString(" Key: ");
        UART_SendHex(ir_key_code);     // 按键编码
        UART_SendString("\r\n");
        ir_receive_complete = 0; // 清除标志
    }
}