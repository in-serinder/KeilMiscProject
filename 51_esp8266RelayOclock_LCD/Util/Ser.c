#include <Ser.h>

#define UART_BAUDRATE 9600 // 串口波特率
#define BUFFER_SIZE 40     // 增大缓冲区，避免溢出

unsigned char receivedData;
unsigned char xdata recvBuffer[BUFFER_SIZE]; // 缓冲区增大到50字节
unsigned int recvLen = 0;
bit recvComplete = 0;
bit validMessage = 0;

void UART_Init()
{
    // SCON = 0x50;                                      // 8位数据，允许接收
    // TMOD |= 0x20;                                     // 定时器1模式2
    //                                                   // TH1 = 256 - (11059200 / 12 / 32 / UART_BAUDRATE); // 波特率初值
    // TH1 = 256 - (12000000 / 12 / 32 / UART_BAUDRATE); // 波特率初值
    // TL1 = TH1;
    // TR1 = 1; // 启动定时器1
    // ES = 1;  // 启用串口中断（修正！）
    // EA = 1;  // 启用总中断（必须！）

    SCON = 0x50;  // 8位数据，可变波特率
    TMOD |= 0x20; // 定时器1模式2（自动重装）

    // 9600波特率@12MHz（12T模式）
    TH1 = 0xFD; // 初值
    TL1 = 0xFD; // 自动重装值

    TR1 = 1; // 启动定时器
    ES = 1;  // 禁止串口中断
    EA = 1;
}

void UART_SendChar(char c)
{
    SBUF = c;
    while (!TI)
        ;
    TI = 0;
}

void UART_SendString(char *str)
{
    while (*str)
    {
        UART_SendChar(*str);
        str++;
    }
}

void UART_ISR() interrupt 4
{
    static bit expectStart = 1; // 等待起始符状态
    unsigned char dat;

    if (RI) // 处理接收中断
    {
        RI = 0;
        dat = SBUF;

        if (expectStart) // 等待 '>' 阶段
        {
            if (dat == '>') // 检测到起始符
            {
                recvLen = 0;
                recvBuffer[recvLen++] = dat;
                expectStart = 0;
                validMessage = 1;
            }
            // 非 '>' 则忽略，不做任何处理
        }
        else // 接收消息内容阶段
        {
            if (dat == '\n') // 检测到结束符 '\n'
            {
                recvBuffer[recvLen] = '\0';
                recvComplete = 1;
                expectStart = 1;
            }
            else
            {

                if (recvLen < BUFFER_SIZE - 1)
                {
                    recvBuffer[recvLen++] = dat;
                }
                else
                {
                    expectStart = 1;
                    validMessage = 0;
                    recvLen = 0;
                }
            }
        }
    }
}

char *getData()
{
    return (char *)recvBuffer;
}

bit getRec()
{
    if (recvComplete && validMessage)
    {
        recvComplete = 0; // 重置完成标志
        return 1;
    }
    return 0;
}

void ClearBuffer()
{
    recvLen = 0;
    validMessage = 0;
}