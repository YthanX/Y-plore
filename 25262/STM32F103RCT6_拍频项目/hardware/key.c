/**
  ******************************************************************************
  * @file    key.c
  * @brief   KEY0 外部中断与 KEY1 软件消抖
  ******************************************************************************
  */

#include "key.h"

/*
 * KEY0 状态由 EXTI 中断写、主循环读，因此使用 volatile；
 * KEY1 完全在主循环内采样，不需要 volatile。
 */

/* KEY0 接 PC5，负责菜单页面切换。 */
#define KEY0_PIN GPIO_Pin_5
/* KEY1 接 PA15，负责进入编辑和保存参数。 */
#define KEY1_PIN GPIO_Pin_15
/* 外接示波器按键接 PC4，低电平按下。 */
#define KEY_SCOPE_PIN GPIO_Pin_4

/* KEY0 中断已经发生，等待主循环取走事件。 */
static volatile uint8_t s_key0_pending;
/* 只有按键释放后才重新置 1，防止一次长按重复触发。 */
static volatile uint8_t s_key0_armed = 1;
/* KEY1 最近 8 次采样历史，1 表示释放，0 表示按下。 */
static uint8_t s_key1_history = 0xFF;
/* KEY1 按下事件已经上报，等待稳定释放后解锁。 */
static uint8_t s_key1_latched;
/* PC4 示波器按键使用与 KEY1 相同的 8 次采样消抖。 */
static uint8_t s_scope_history = 0xFF;
static uint8_t s_scope_latched;

/**
 * @brief  初始化 KEY0 外部中断和 KEY1 轮询输入
 */
void Key_Init(void)
{
    /* gpio、exti、nvic 分别保存引脚、中断线和中断优先级配置。 */
    GPIO_InitTypeDef gpio;
    EXTI_InitTypeDef exti;
    NVIC_InitTypeDef nvic;

    /* EXTI 映射需要 AFIO，两个按键分别位于 GPIOA 和 GPIOC。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO |
                            RCC_APB2Periph_GPIOA |
                            RCC_APB2Periph_GPIOC, ENABLE);

    /* PA15 默认属于 JTAG；关闭 JTAG 但保留 SWD 调试。 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    /* 两个按键均为低电平按下，因此配置内部上拉。 */
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    /* PC4 和 PC5 都在 GPIOC，合并初始化为上拉输入。 */
    gpio.GPIO_Pin = KEY0_PIN | KEY_SCOPE_PIN;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin = KEY1_PIN;
    GPIO_Init(GPIOA, &gpio);

    /* 把 EXTI5 映射到 PC5，并在下降沿产生 KEY0 中断。 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource5);
    exti.EXTI_Line = EXTI_Line5;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    /* 按键中断优先级低于 ADC DMA，避免影响实时采样。 */
    nvic.NVIC_IRQChannel = EXTI9_5_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

/**
 * @brief  在主循环中取得一个稳定的按键事件
 * @retval KEY_EVENT_MENU、KEY_EVENT_EDIT 或 KEY_EVENT_NONE
 */
KeyEvent Key_TakeEvent(void)
{
    /* KEY1 为低电平按下，先转换成便于移位的 0/1 电平。 */
    uint8_t key1_level = GPIO_ReadInputDataBit(GPIOA, KEY1_PIN) == Bit_SET ? 1 : 0;
    /* 外接示波器按键同样按低电平有效处理。 */
    uint8_t scope_level = GPIO_ReadInputDataBit(GPIOC, KEY_SCOPE_PIN) == Bit_SET ? 1 : 0;

    /* KEY0 检测到释放后重新 armed，下一次下降沿才允许再次上报。 */
    if (GPIO_ReadInputDataBit(GPIOC, KEY0_PIN) == Bit_SET)
    {
        s_key0_armed = 1;
    }

    /* KEY0 事件优先返回，业务层无需理解 EXTI 标志。 */
    if (s_key0_pending != 0)
    {
        s_key0_pending = 0;
        return KEY_EVENT_MENU;
    }

    /* 连续 8 次采到低电平才确认按下，连续高电平后才允许再次触发。 */
    s_key1_history = (uint8_t)((s_key1_history << 1) | key1_level);
    if ((s_key1_history == 0x00) && (s_key1_latched == 0))
    {
        /* 第一次稳定按下产生一个 EDIT 事件，并锁存防止长按重复。 */
        s_key1_latched = 1;
        return KEY_EVENT_EDIT;
    }

    if (s_key1_history == 0xFF)
    {
        /* 连续 8 次高电平表示稳定释放，可以接受下一次按下。 */
        s_key1_latched = 0;
    }

    /* 示波器按键连续 8 次低电平后只上报一次事件。 */
    s_scope_history = (uint8_t)((s_scope_history << 1) | scope_level);
    if ((s_scope_history == 0x00) && (s_scope_latched == 0U))
    {
        s_scope_latched = 1U;
        return KEY_EVENT_SCOPE;
    }
    if (s_scope_history == 0xFF)
    {
        s_scope_latched = 0U;
    }

    return KEY_EVENT_NONE;
}

/**
 * @brief  KEY0 对应的 EXTI5 中断服务函数
 * @note   中断中只置事件标志，不执行页面切换或屏幕刷新。
 */
void EXTI9_5_IRQHandler(void)
{
    /* EXTI9_5 共享多个中断线，必须先确认本次来自 Line5。 */
    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        /* 中断中只记录事件，菜单切换等业务统一放到 BeatApp 处理。 */
        if (s_key0_armed != 0)
        {
            s_key0_pending = 1;
            s_key0_armed = 0;
        }
        /* 写 1 清除挂起位，否则退出中断后会立即再次进入。 */
        EXTI_ClearITPendingBit(EXTI_Line5);
    }
}
