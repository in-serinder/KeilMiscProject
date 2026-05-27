#include "Timer.h"

void Timer0_Init(void) {
    // Placeholder for Timer0 initialization code

    TMOD |= 0x01; // Set Timer0 to Mode 1 (16-bit timer)

    TH0 = 0xFC;   // Load high byte for 1ms delay (assuming 12MHz clock)
    TL0 = 0x66;   // Load low byte for 1ms delay
    ET0 = 1;      // Enable Timer0 interrupt
    EA=1;       // Enable global interrupts
    TR0 = 1;      // Start Timer0
}   


