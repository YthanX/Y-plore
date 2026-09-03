#include "stm32f10x.h"                  // Device header


void IC_init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);//初始化tim3通道一对应的引脚是pa6
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);//初始化tim3
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;//input_pull_up
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	
	TIM_InternalClockConfig(TIM3);//采用内部时钟tim2
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
	TIM_TimeBaseInitStruct.TIM_ClockDivision=TIM_CKD_DIV1;
	TIM_TimeBaseInitStruct.TIM_CounterMode=TIM_CounterMode_Up;
	TIM_TimeBaseInitStruct.TIM_Period=65536-1;//CRR
	TIM_TimeBaseInitStruct.TIM_Prescaler=720-1;//PSC
	TIM_TimeBaseInitStruct.TIM_RepetitionCounter=0;
	TIM_TimeBaseInit(TIM3,&TIM_TimeBaseInitStruct);
	
	//初始化捕获器IC
	TIM_ICInitTypeDef TIM_ICInitStruct;
	TIM_ICInitStruct.TIM_Channel=TIM_Channel_1;
	TIM_ICInitStruct.TIM_ICFilter=0x7;
	TIM_ICInitStruct.TIM_ICPolarity=TIM_ICPolarity_Rising;
	TIM_ICInitStruct.TIM_ICPrescaler=TIM_ICPSC_DIV1;
	TIM_ICInitStruct.TIM_ICSelection=TIM_ICSelection_DirectTI;
	TIM_PWMIConfig(TIM3,&TIM_ICInitStruct);
	
	//触发源配置
	TIM_SelectInputTrigger(TIM3,TIM_TS_TI1FP1);
	
	//从模式
	TIM_SelectSlaveMode(TIM3,TIM_SlaveMode_Reset);
	
	TIM_Cmd(TIM3,ENABLE);
}

uint32_t IC_GetFrequency(void)
{
	 return 100000/(TIM_GetCapture1(TIM3)+1);
}

uint32_t IC_GetDuty(void)
{
	return (TIM_GetCapture2(TIM3)+1)*10000/(TIM_GetCapture1(TIM3)+1);
}
