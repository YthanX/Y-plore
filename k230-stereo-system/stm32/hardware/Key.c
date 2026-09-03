#include "stm32f10x.h"                  // Device header
#include "Delay.h"
typedef struct 
{
	uint32_t RCC_APB2Periph_GPIO_X;
	GPIO_TypeDef * GPIO_x;
	uint16_t GPIO_Pin_x;
	GPIOMode_TypeDef GPIO_Mode;
}KEY;
void Key_Init(KEY key)
{
	RCC_APB2PeriphClockCmd(key.RCC_APB2Periph_GPIO_X, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = key.GPIO_Mode; 
	GPIO_InitStructure.GPIO_Pin = key.GPIO_Pin_x;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(key.GPIO_x, &GPIO_InitStructure);
}


uint8_t Key_GetNum(KEY key)
{
	uint8_t KeyNum = 1;
	if (GPIO_ReadInputDataBit(key.GPIO_x, key.GPIO_Pin_x) == 0)
	{
		Delay_ms(20);
		while (GPIO_ReadInputDataBit(key.GPIO_x, key.GPIO_Pin_x) == 0);
		Delay_ms(20);
		KeyNum = 0;
	}
	return KeyNum;
}
