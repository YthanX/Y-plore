#ifndef __KEY_H
#define __KEY_H
typedef struct 
{
	uint32_t RCC_APB2Periph_GPIO_X;
	GPIO_TypeDef * GPIO_x;
	uint16_t GPIO_Pin_x;
	GPIOMode_TypeDef GPIO_Mode;
}KEY;

void Key_Init(KEY key);

uint8_t Key_GetNum(KEY key);
#endif
