#include <Ser.h>
#include <Timer.h>
#include <stdio.h>
#include <Util.h>
#include <Max7219.h>
#include <TM1637.h>

/* ----串口消息格式----
 *时间戳: 2023-10-24-16:00:00^
 *继电器状态 000^
 *温湿度 00.00^00.00^
 *完全格式 2023-10-24-16:00:00^000^00.00^00.00^
 */

sbit Relay1 = P0 ^ 3;
sbit Relay2 = P0 ^ 2;
sbit Relay3 = P0 ^ 4;

sbit RelayLED1 = P1 ^ 1;
sbit RelayLED2 = P1 ^ 2;
sbit RelayLED3 = P1 ^ 4;
sbit WarnLed = P1 ^ 7;

sbit infrared = P1 ^ 3;

sbit RelayButton = P0 ^ 0;
sbit SettingButton = P0 ^ 1;

sbit BuzzerS1 = P0 ^ 0;

void main()
{
    char xdata *msg, *timestr;
    struct UtilOBJ xdata dat;
    unsigned int time;
    unsigned char i;
    unsigned char *IRdata;
    unsigned char xdata relaySetPos = 1, Timercount = 0;
    int xdata timecount = 0;
    bit dateOrTempFlag = 0;
    bit bitlogrelay = 0;
    bit settingflag = 0;
    bit flip = 0;

    ///////////////////////////////////////////
    UART_Init();
    // 初始化串口
    TM1637_Init();
    Max7219_Init();

    InitExternalInterrupt0();

    Relay1 = 0;
    Relay2 = 0;
    Relay3 = 0;
    RelayLED1 = 0;
    RelayLED2 = 0;
    RelayLED3 = 0;
    WarnLed = 0;
    BuzzerS1 = 0;

    //////////////////////////////////////////

    UART_SendString("51 Ready!\r\n");

    //////////////////////////////////////////
    while (1)
    {
        // timecount++;
        // if (timecount % 100 == 0)
        // {
        // }

        // UART_SendString("Hello, World!\r\n");
        if (getRec())
        {

            // 串口获取内部是标准的1s计时器
            timecount++;
            Timercount++;
            msg = getData();
            dat = getUtil(msg);

            // sprintf(timestr, "%d", ((dat.Time[0] - '0') * 10 + (dat.Time[1] - '0')) * 100 + ((dat.Time[2] - '0') * 10 + (dat.Time[3] - '0')));
            sprintf(timestr, "%d%d%d%d", dat.Time[0], dat.Time[1], dat.Time[2], dat.Time[3]);

            time = ((dat.Time[0] - '0') * 10 + (dat.Time[1] - '0')) * 100 + ((dat.Time[2] - '0') * 10 + (dat.Time[3] - '0'));

            //////////////////////////////////////
            UART_SendString(msg);
            UART_SendString(timestr);
            UART_SendChar('\n');
            // UART_SendString(dat.Relay1);
            UART_SendChar(dat.Relay1 ? '1' : '0');
            UART_SendChar('\n');
            // UART_SendString(dat.Relay2);
            UART_SendChar(dat.Relay2 ? '1' : '0');
            UART_SendChar('\n');
            // UART_SendString(dat.Relay3);
            UART_SendChar(dat.Relay3 ? '1' : '0');
            UART_SendChar('\n');

            // Max7921StringDisplay(dat.Date);
            // Max7219StringDisplay(dat.Date);
            /*时间与温湿度*/
            // Max7219TemperatureAndHumitidyDisplay(dat.Temperature, dat.Humidity);
            if (dateOrTempFlag)
            {
                Max7219DateDisplay(dat.Date);
            }
            else
            {
                Max7219TemperatureAndHumitidyDisplay(dat.Temperature, dat.Humidity);
            }

            if (timecount % 10 == 0)
            {
                dateOrTempFlag = !dateOrTempFlag;
            }

            TM1637_Display4Num(time, flip);
            // TM1637_StringDisplay(dat.Time, flip);
            flip = !flip; // 翻转显示时间冒号
            ClearBuffer();

            // Max7219_Clear();
            /*继电器*/
            Relay1 = dat.Relay1;
            Relay2 = dat.Relay2;
            Relay3 = dat.Relay3;
            /*继电器灯光继承*/
            RelayLED1 = Relay1;
            RelayLED2 = Relay2;
            RelayLED3 = Relay3;
        }

        if (getIRFlag())
        {
            IRdata = getIRData();

            // 发送标题
            UART_SendString("IR Code: ");

            // 逐个字节转换为16进制字符串发送
            for (i = 0; i < 4; i++)
            {
                // 发送高4位
                UART_SendChar((IRdata[i] >> 4) < 10 ? (IRdata[i] >> 4) + '0' : (IRdata[i] >> 4) + 'A' - 10);
                // 发送低4位
                UART_SendChar((IRdata[i] & 0x0F) < 10 ? (IRdata[i] & 0x0F) + '0' : (IRdata[i] & 0x0F) + 'A' - 10);
                UART_SendChar(' ');
            }

            UART_SendString("\r\n"); // 换行
        }

        /*处理按钮调节*/
        if (SettingButton)
        {
            settingflag = 1;
            Timercount = 0;                   // 重置计时器
            dateOrTempFlag = !dateOrTempFlag; // 直接翻转
            dat.WarnFlag = 0;
        }
        // 警告状态
        if (dat.WarnFlag)
        {
            WarnLed = 1;
            BuzzerS1 = 1;
        }
        else
        {
            WarnLed = 0;
            BuzzerS1 = 0;
        }

        if (settingflag)
        {
            // Delay_ms(1000); // 处于设置模式时延时
            WarnLed = 1;

            relaySetPos++;
            if (relaySetPos >= 3)
            {
                relaySetPos = 1;
            }

            switch (relaySetPos)
            {
            case 1:
                RelayLED1 = 1;
                RelayLED2 = 0, RelayLED3 = 0;
                break;
            case 2:
                RelayLED1 = 0;
                RelayLED2 = 1, RelayLED3 = 0;
                break;
            case 3:
                RelayLED1 = 0;
                RelayLED2 = 0, RelayLED3 = 1;
                break;

            default:
                break;
            }

            if (RelayButton == 1)
            {
                RelayBuzzer();
                switch (relaySetPos)
                {
                case 1:
                    Relay1 = (Relay1 == 1 ? 0 : 1);
                    break;
                case 2:
                    Relay2 = (Relay2 == 1 ? 0 : 1);
                    break;
                case 3:
                    Relay3 = (Relay3 == 1 ? 0 : 1);
                    break;

                default:
                    break;
                }
            }

            // 十秒取消
            if (Timercount >= 10)
            {
                settingflag = 0;
                WarnLed = 0;
            }
        }

        /*继电器灯光继承*/
        RelayLED1 = Relay1;
        RelayLED2 = Relay2;
        RelayLED3 = Relay3;

        // Delay_ms(10);
        // Delay_ms(150);
        Delay1000ms1();
    }
    //////////////////////////////////////////
}