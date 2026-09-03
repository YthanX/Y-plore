#include "stm32f10x.h"                  // Device header

void PWM_init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	//初始化tim2_chn1对应引脚pa0
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP ; //注意是复用推挽
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1; //| GPIO_Pin_2; //PA0,PA1,PA2
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure); 

	
	TIM_InternalClockConfig(TIM2);//采用内部时钟tim2
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
	TIM_TimeBaseInitStruct.TIM_ClockDivision=TIM_CKD_DIV1;
	TIM_TimeBaseInitStruct.TIM_CounterMode=TIM_CounterMode_Up;
	TIM_TimeBaseInitStruct.TIM_Period=20000-1;//ARR
	TIM_TimeBaseInitStruct.TIM_Prescaler=72-1;//PSC
	TIM_TimeBaseInitStruct.TIM_RepetitionCounter=0;
	TIM_TimeBaseInit(TIM2,&TIM_TimeBaseInitStruct);
	
	//不用nvic管理中断喔
	TIM_OCInitTypeDef TIM_OCInitStructre;
	TIM_OCStructInit(&TIM_OCInitStructre);
	TIM_OCInitStructre.TIM_OCMode=TIM_OCMode_PWM1;
	TIM_OCInitStructre.TIM_OCPolarity=TIM_OCPolarity_High;
	TIM_OCInitStructre.TIM_OutputState=TIM_OutputState_Enable;
	TIM_OCInitStructre.TIM_Pulse=0;//设置ccr寄存器，就是比较器
	TIM_OC1Init(TIM2,&TIM_OCInitStructre);//外设tim2有四个channel，每个通道对应复用一个引脚，可以查看数据手册，最特殊的是chn1,
	//他和ETR共复用GPIA0
	TIM_OC2Init(TIM2,&TIM_OCInitStructre);//chn2
	//TIM_OC3Init(TIM2,&TIM_OCInitStructre);//chn3
	
	TIM_Cmd(TIM2,ENABLE);
	
}
void PWM_Set_PSC(uint16_t Prescaler)
{
	TIM_PrescalerConfig(TIM2,Prescaler,TIM_PSCReloadMode_Update);
}
void PWM_Set_Compare_chn1(uint16_t Compare)
{
	TIM_SetCompare1(TIM2,Compare);
}
void PWM_Set_Compare_chn2(uint16_t Compare)
{
	TIM_SetCompare2(TIM2,Compare);
}
/*void PWM_Set_Compare_chn3(uint16_t Compare)
{
	TIM_SetCompare3(TIM2,Compare);
}*/

