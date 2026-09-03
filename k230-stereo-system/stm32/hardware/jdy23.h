#ifndef  __JDY23_H__
#define  __JDY23_H__
#include "stdio.h"	
#include "stm32f10x.h"
void Jdy23_USART3_Init(uint32_t bound);

void Jdy23_USART3_Send_Byte(uint8_t Data); 

void Jdy23_USART3_Send_nByte(uint8_t *Data, uint16_t size) ;

void Jdy23_USART3_Send_Str(uint8_t *Data) ;

#endif

