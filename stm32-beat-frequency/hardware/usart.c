/**
  ******************************************************************************
  * @file    usart.c
  * @brief   USART1 中断接收、环形缓冲和 DMA 非阻塞发送
  ******************************************************************************
  */

#include "usart.h"
#include <stdio.h>
#include <string.h>

/*
 * g_ 前缀表示文件中的模块状态。它们仍然声明为 static，
 * 因此不会暴露成其他文件可直接访问的全局符号。
 * head/tail/busy 会在中断和主循环之间共享，所以使用 volatile。
 */

/* RX 环形缓冲容量，足够保存短命令和普通调试输入。 */
#define USART1_RX_BUFFER_SIZE 128
/* TX DMA 内部静态缓冲容量，也是单次发送长度上限。 */
#define USART1_TX_DMA_BUFFER_SIZE 256

/* RX 中断写入、主循环读取的环形字节缓冲。 */
static volatile uint8_t g_usart1_rx_buffer[USART1_RX_BUFFER_SIZE];
/* head 指向下一次写入位置。 */
static volatile uint16_t g_usart1_rx_head = 0;
/* tail 指向下一次读取位置。 */
static volatile uint16_t g_usart1_rx_tail = 0;
/* 缓冲写满时丢弃新字节，并累计溢出次数。 */
static volatile uint16_t g_usart1_rx_overflow = 0;
/* DMA 使用模块内部缓冲，避免调用方局部数组生命周期不足。 */
static uint8_t g_usart1_tx_dma_buffer[USART1_TX_DMA_BUFFER_SIZE];
/* 非零表示 DMA1_CH4 正在发送，禁止启动第二次发送。 */
static volatile uint8_t g_usart1_tx_dma_busy = 0;

/**
 * @brief  计算 RX 环形缓冲的下一个索引
 */
static uint16_t USART1_RxNextIndex(uint16_t index)
{
    index++;
    if (index >= USART1_RX_BUFFER_SIZE)
    {
        /* 到达数组末尾后回到第 0 项。 */
        index = 0;
    }
    return index;
}

/**
 * @brief  初始化 USART1、RXNE 中断和 DMA1_CH4 TX
 * @param  baudrate 串口波特率，本工程使用 115200
 */
void USART1_Init(uint32_t baudrate)
{
    /* 四个结构体分别配置 GPIO、USART、DMA 和 NVIC。 */
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* USART1 和 PA9/PA10 位于 APB2，DMA1 使用 AHB 时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO | RCC_APB2Periph_USART1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PA9 为 TX，复用推挽输出。 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA10 为 RX，浮空输入。 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 8 数据位、1 停止位、无校验、无硬件流控。 */
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    /* DMA1_CH4 是 USART1_TX 固定映射，使用普通模式而非循环模式。 */
    DMA_DeInit(DMA1_Channel4);
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)g_usart1_tx_dma_buffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    /* 初始化时先给合法占位值，真正发送前再写入实际 length。 */
    DMA_InitStructure.DMA_BufferSize = 1U;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_Low;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel4, &DMA_InitStructure);
    /* 传输完成和传输错误都进入中断，用于释放 busy 状态。 */
    DMA_ITConfig(DMA1_Channel4, DMA_IT_TC | DMA_IT_TE, ENABLE);
    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);

    /* 每收到一个字节触发 RXNE 中断并写入软件环形缓冲。 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* USART RX 优先级低于 ADC DMA，高于普通按键事件。 */
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* TX DMA 只负责收尾，使用更低优先级。 */
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

/**
 * @brief  阻塞发送一个字节
 */
void USART1_SendByte(uint8_t byte)
{
    /* 写 DR 后等待 TXE，表示发送数据寄存器可以接收下一字节。 */
    USART_SendData(USART1, byte);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    {
    }
}

/**
 * @brief  逐字节阻塞发送数组
 */
void USART1_SendArray(const uint8_t *data, uint16_t length)
{
    /* i 遍历待发送数组。 */
    uint16_t i;

    /* 空指针直接返回。 */
    if (data == 0)
    {
        return;
    }

    /* 该接口适合启动阶段或短数据，长诊断优先使用 DMA。 */
    for (i = 0; i < length; i++)
    {
        USART1_SendByte(data[i]);
    }
}

/**
 * @brief  阻塞发送 C 字符串
 */
void USART1_SendString(const char *str)
{
    if (str == 0)
    {
        return;
    }

    /* 逐字节发送，直到字符串结束符。 */
    while (*str != '\0')
    {
        USART1_SendByte((uint8_t)*str);
        str++;
    }
}

/**
 * @brief  复制数据并启动 USART1 TX DMA
 * @retval 1 成功启动，0 表示参数无效或通道忙
 */
uint8_t USART1_SendDma(const uint8_t *data, uint16_t length)
{
    /* 同一时间只允许一个 DMA 发送任务，避免两段文本交叉。 */
    if ((data == 0) || (length == 0U) ||
        (length > USART1_TX_DMA_BUFFER_SIZE) || (g_usart1_tx_dma_busy != 0U))
    {
        return 0U;
    }

    /* 先复制到模块内部缓冲，避免调用方局部数组提前失效。 */
    memcpy(g_usart1_tx_dma_buffer, data, length);
    /* 普通模式每次发送前都要关闭通道、清标志并重装传输计数。 */
    DMA_Cmd(DMA1_Channel4, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL4);
    DMA_SetCurrDataCounter(DMA1_Channel4, length);
    /* 先置 busy 再使能 DMA，防止主循环立即发起第二次发送。 */
    g_usart1_tx_dma_busy = 1U;
    DMA_Cmd(DMA1_Channel4, ENABLE);
    return 1U;
}

/** @brief 查询 TX DMA 是否正在使用内部发送缓冲 */
uint8_t USART1_TxDmaBusy(void)
{
    return g_usart1_tx_dma_busy;
}

/** @brief 查询 RX 环形缓冲是否非空 */
uint8_t USART1_Available(void)
{
    return (g_usart1_rx_head != g_usart1_rx_tail) ? 1 : 0;
}

/**
 * @brief  从 RX 环形缓冲取出一个字节
 * @param  byte 接收字节的目标地址
 */
uint8_t USART1_ReadByte(uint8_t *byte)
{
    /* head==tail 表示缓冲为空。 */
    if ((byte == 0) || (g_usart1_rx_head == g_usart1_rx_tail))
    {
        return 0;
    }

    /* 读取 tail 所指字节，再推进 tail 释放该位置。 */
    *byte = g_usart1_rx_buffer[g_usart1_rx_tail];
    g_usart1_rx_tail = USART1_RxNextIndex(g_usart1_rx_tail);
    return 1;
}

/** @brief 清空 RX 缓冲和溢出统计 */
void USART1_ClearRxBuffer(void)
{
    g_usart1_rx_head = 0;
    g_usart1_rx_tail = 0;
    g_usart1_rx_overflow = 0;
}

/** @brief 返回接收缓冲写满的累计次数 */
uint16_t USART1_GetRxOverflowCount(void)
{
    return g_usart1_rx_overflow;
}

/** @brief 基础串口回显任务，当前拍频应用未主动调用 */
void USART1_EchoTask(void)
{
    /* byte 保存从 RX 环形缓冲读出的当前字节。 */
    uint8_t byte;

    while (USART1_ReadByte(&byte) != 0)
    {
        USART1_SendByte(byte);
    }
}

/**
 * @brief  USART1 接收中断
 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        /* 读 DR 会清除 RXNE，并取得本次字节。 */
        uint8_t byte = (uint8_t)USART_ReceiveData(USART1);
        /* next 是写入本字节后 head 应移动到的位置。 */
        uint16_t next = USART1_RxNextIndex(g_usart1_rx_head);

        /* head 的下一格等于 tail 时表示环形缓冲已满。 */
        if (next != g_usart1_rx_tail)
        {
            g_usart1_rx_buffer[g_usart1_rx_head] = byte;
            g_usart1_rx_head = next;
        }
        else
        {
            /* 缓冲满时不覆盖尚未读取的数据，只记录一次溢出。 */
            g_usart1_rx_overflow++;
        }
    }

    /* 发生硬件 overrun 时读 DR 清除错误，避免接收中断停滞。 */
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET)
    {
        (void)USART_ReceiveData(USART1);
    }
}

/**
 * @brief  USART1 TX DMA 完成/错误中断
 */
void DMA1_Channel4_IRQHandler(void)
{
    /* 无论正常完成还是传输错误，都要释放 busy 状态。 */
    if (DMA_GetITStatus(DMA1_IT_TC4) != RESET)
    {
        /* 正常完成：清标志、关闭普通模式通道、释放 busy。 */
        DMA_ClearITPendingBit(DMA1_IT_TC4);
        DMA_Cmd(DMA1_Channel4, DISABLE);
        g_usart1_tx_dma_busy = 0U;
    }

    if (DMA_GetITStatus(DMA1_IT_TE4) != RESET)
    {
        /* 传输错误也必须释放 busy，否则后续串口将永久无法发送。 */
        DMA_ClearITPendingBit(DMA1_IT_TE4);
        DMA_Cmd(DMA1_Channel4, DISABLE);
        g_usart1_tx_dma_busy = 0U;
    }
}

/**
 * @brief  重定向标准库 printf 到 USART1 阻塞发送
 */
int fputc(int ch, FILE *f)
{
    (void)f;
    USART1_SendByte((uint8_t)ch);
    return ch;
}

