#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdarg.h>
uint8_t Serial_Txpacket[4];
uint16_t RX_BUF_SIZE=0;

void Serial_init(uint8_t* Serial_Rxpacket, uint16_t Serial_RXlength)	
{
	RX_BUF_SIZE = Serial_RXlength;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate= 115200 ;
	USART_InitStructure.USART_HardwareFlowControl=USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode=USART_Mode_Tx|USART_Mode_Rx;
	USART_InitStructure.USART_Parity=USART_Parity_No;
	USART_InitStructure.USART_StopBits=USART_StopBits_1;
	USART_InitStructure.USART_WordLength=USART_WordLength_8b;
	USART_Init(USART1, &USART_InitStructure);
	
	//配置DMA
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

	//配置DMA1通道1
    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR; //外设地址
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)Serial_Rxpacket; //内存地址  由a->b,这里将adc的值搬到ad_value里面了
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC; //数据传输方向，外设作为源
    DMA_InitStructure.DMA_BufferSize = RX_BUF_SIZE; //数据缓冲区大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; //外部一次四个字节
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable; //内存地址自增，允许自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; //外设数据大小,字节
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; //内存数据大小,传输字节
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;//DMA模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High; //DMA优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable; //不使能软件出发memory to memory，用adc触发
    DMA_Init(DMA1_Channel5, &DMA_InitStructure); //初始化DMA1通道1

	DMA_Cmd(DMA1_Channel5, ENABLE);//使能DMA 
	USART_Cmd(USART1,ENABLE);
	USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE); // 使能USART的DMA请求

	USART_ITConfig(USART1, USART_IT_IDLE,ENABLE);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel=USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=2;//两位父优先级，两位子优先级，都一样对比硬件编号
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=1;
	NVIC_Init(&NVIC_InitStructure);
}

//发送字节 0x00, 0x01...
void Serial_send_bytes(uint8_t byte)
{
  USART_SendData(USART1, byte);
	
	while(USART_GetFlagStatus(USART1,USART_FLAG_TXE)==RESET);
	
	//读写均不用清零USART_ClearFlag(USART1,USART_FLAG_TXE);
}
//发送字节数组
void Serial_send_Array(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for( i=0;i<Length;i++)
	{
		Serial_send_bytes(Array[i]);
	}
	
}
//发送字符串
void Serial_send_String(char *String)
{
	uint16_t i;
	for( i=0; String[i]!='\0';i++)
	{
		Serial_send_bytes(String[i]);
	}
}
//发送数据包
void Serial_send_packet(void)
{
	Serial_send_bytes(0xFF);//帧头
	
	Serial_send_Array(Serial_Txpacket,4);
	
	Serial_send_bytes(0xFE);//帧头
}

//工具函数 power n 指数; 幂
uint32_t Untils_pow(uint32_t X, uint32_t Y)
{
	uint32_t result=1;
	while(Y--)
	{
		result *= X;
	}
	return result;
}

void Serial_send_Num(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for( i=0 ; i<Length; i++)
	{
		Serial_send_bytes(Number/Untils_pow(10, Length-i-1) %10 + '0');
	}
}
#pragma import(__use_no_semihosting)

 struct __FILE
 {
 	int handle;
 };
 FILE __stdout;
 _sys_exit(int x)
 {
	x=x;
 }

 int fgetc(FILE *f) 
{
 	while(USART_GetFlagStatus(USART1, USART_FLAG_RXNE)==RESET);
 	return ((int)USART_ReceiveData(USART1));	
}
int fputc(int ch, FILE *f)
{
	Serial_send_bytes(ch);
	return ch;
}
//格式化输出
//使用可变参数列表
void Serial_Printf(char *format,...)
{
		char String[100];
	  	va_list arg;
		va_start(arg,format);
		vsprintf(String,format,arg);
		va_end(arg);
		Serial_send_String(String);
			
}

void Serial_DMA_transfer(void)
{
	while(DMA_GetFlagStatus(DMA1_FLAG_TC5) == RESET); //等待传输完成标志位被置位
    DMA_ClearFlag(DMA1_FLAG_TC5); //清除传输完成标志
}

// void USART1_IRQHandler(void)
// {
// 	if(USART_GetITStatus(USART1, USART_IT_IDLE) != RESET) //如果空闲中断发生
// 	{
// 		volatile uint32_t temp;
//         temp = USART1->SR;
//         temp = USART1->DR;
//         Serial_RECEIVE_FLAG = 1; // 有新数据包
// 	}
// }
