#ifndef COM_H_
#define COM_H_

/* USART1 is initialized by existing usart.c before Com_Init(). */
void Com_Init(void);
void Com_UART_IRQHandler(void);
/* Main loop / task 10 ms only. Flash writes never run in the UART ISR. */
void Com_MainFunction(void);

#endif
