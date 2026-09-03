#ifndef __SERIAL__H
#define __SERIAL__H
#define RX_BUF_SIZE 87 // 实际接受87固定字节
#define Header 0xFF
#define Footer 0xFE


#include <stdio.h>
void Serial_init(uint8_t *Serial_Rxpacket, uint16_t Serial_RXlength);
void Serial_send_bytes(uint8_t byte);
void Serial_send_String(char *String);;
void Serial_send_Array(uint8_t *Array, uint16_t Length);
void Serial_send_Num(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format,...);//高级用法

extern uint8_t Serial_Txpacket[];
void Serial_send_packet(void);
void Serial_DMA_transfer(void);

//中断接收数据
#endif

