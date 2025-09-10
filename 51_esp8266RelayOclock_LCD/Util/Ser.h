#include <stc89c52.h>

void UART_Init();
void UART_SendChar(char c);
void UART_SendString(char *str);
char *getData();
bit getRec();
void ClearBuffer();