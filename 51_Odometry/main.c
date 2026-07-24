#include "stc89c52.h"
#include "Display.h"
#include "obo.h"
#include "Delay.h"
#include "key.h"

#define LAST_MODE_ADDR 0x01 //存储上次模式的地址

bit Mode_Flag = 0; //0:SM_Mode, 1:Obometry_Mode
bit Reset_Flag = 0; //0:不重置, 1:需重置

void main(void)
{
    Display_Init();
    // 从EEPROM中读取上次模式
    Mode_Flag = EEPROM_ReadByte(LAST_MODE_ADDR);

    while(1){
        
        if(Mode_Flag){
            Display_SM_Mode(OBO_GET_SPEED(), OBO_GET_Miles());
        }
        else{
            Display_Obometry_Mode(OBO_GET_SPEED());

        }


        // 重置里程
        Reset_Flag = Key_Scan();
        if(Reset_Flag){
            OBO_SET_Millis(0);
            Reset_Flag = 0;
        }
        // 保存当前模式到EEPROM
        if(Mode_Flag != EEPROM_ReadByte(LAST_MODE_ADDR)){
            EEPROM_WriteByte(LAST_MODE_ADDR, Mode_Flag);
        }
				        Delay_ms(1000);

        }
    }



void INT1_Isr(void) interrupt 2
{
    Mode_Flag = !Mode_Flag;
}
