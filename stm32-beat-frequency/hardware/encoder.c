/**
  ******************************************************************************
  * @file    encoder.c
  * @brief   TIM3 Encoder mode 旋转量读取
  ******************************************************************************
  */

#include "encoder.h"

/*
 * 本模块使用 static 保存“上次计数”和“不足一档的余数”。
 * 如果把它们定义成普通局部变量，每次 Encoder_GetDelta() 返回后状态会丢失。
 */

/* 常见机械编码器一个物理档位对应 A/B 两相的 4 次计数变化。 */
#define ENCODER_COUNTS_PER_STEP 4

/* 上一次读取的 TIM3 计数器值。 */
static int16_t s_last_counter;
/* 不足一个机械档位的计数余数，留到下次调用继续累计。 */
static int16_t s_remainder;

/**
 * @brief  把 PA6/PA7 配置为 TIM3 硬件编码器输入
 */
void Encoder_Init(void)
{
    /* gpio、tim、ic 分别是 GPIO、定时器时基和输入捕获配置结构。 */
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tim;
    TIM_ICInitTypeDef ic;

    /* PA6/PA7 属于 GPIOA，TIM3 位于 APB1。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* 编码器公共端接地，因此 A/B 两路使用内部上拉。 */
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    TIM_TimeBaseStructInit(&tim);
    /* 16 位自由计数，方向由编码器接口硬件自动决定。 */
    tim.TIM_Period = 0xFFFF;
    tim.TIM_Prescaler = 0;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &tim);

    TIM_ICStructInit(&ic);
    /* 数字滤波抑制机械触点抖动和短脉冲。 */
    ic.TIM_ICFilter = 10;
    ic.TIM_Channel = TIM_Channel_1;
    TIM_ICInit(TIM3, &ic);
    ic.TIM_Channel = TIM_Channel_2;
    TIM_ICInit(TIM3, &ic);
    /* TI12 同时使用 CH1、CH2 判定旋转方向和计数。 */
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    /* 从计数范围中点开始，给正反两个方向都留出足够空间。 */
    TIM_SetCounter(TIM3, 0x8000);
    s_last_counter = (int16_t)TIM_GetCounter(TIM3);
    s_remainder = 0;
    TIM_Cmd(TIM3, ENABLE);
}

/**
 * @brief  把 TIM3 原始计数差转换为机械档位数
 * @retval 正负值表示两个旋转方向，0 表示尚未形成完整档位
 */
int16_t Encoder_GetDelta(void)
{
    /* int16_t 减法可自然处理 16 位计数器回绕附近的小范围变化。 */
    int16_t current = (int16_t)TIM_GetCounter(TIM3);
    /* raw_delta 为本次与上次读取之间的硬件原始计数变化。 */
    int16_t raw_delta = (int16_t)(current - s_last_counter);
    /* steps 为换算后的完整机械档位数。 */
    int16_t steps;

    /* 常见机械编码器一个档位产生 4 个计数，余数留到下次继续累计。 */
    s_last_counter = current;
    s_remainder += raw_delta;
    /* C 整数除法保留符号，得到完整机械档位数。 */
    steps = s_remainder / ENCODER_COUNTS_PER_STEP;
    /* 扣除已经返回的完整档位，只保留不足 4 count 的部分。 */
    s_remainder -= steps * ENCODER_COUNTS_PER_STEP;

    return steps;
}
