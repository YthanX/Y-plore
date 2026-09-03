/**
  ******************************************************************************
  * @file    usart.h
  * @brief   USART1 收发、TX DMA 和接收环形缓冲接口
  ******************************************************************************
  */

#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"

/** @brief 初始化 USART1、接收中断和 DMA1_CH4 发送通道 */
void USART1_Init(uint32_t baudrate);
/** @brief 阻塞发送一个字节 */
void USART1_SendByte(uint8_t byte);
/** @brief 阻塞发送指定长度的字节数组 */
void USART1_SendArray(const uint8_t *data, uint16_t length);
/** @brief 阻塞发送以 '\0' 结尾的字符串 */
void USART1_SendString(const char *str);

/**
 * @brief  通过 DMA1_CH4 非阻塞发送数据
 * @param  data 待发送数据，函数会先复制到内部缓冲区
 * @param  length 数据长度，最大 256B
 * @retval 1 DMA 已启动
 * @retval 0 参数无效或发送通道正忙
 */
uint8_t USART1_SendDma(const uint8_t *data, uint16_t length);
/** @brief 查询 USART1 TX DMA 是否正在发送 */
uint8_t USART1_TxDmaBusy(void);
/** @brief 查询接收环形缓冲中是否存在数据 */
uint8_t USART1_Available(void);

/**
 * @brief  从接收环形缓冲读取一个字节
 * @retval 1 读取成功
 * @retval 0 当前无数据或参数无效
 */
uint8_t USART1_ReadByte(uint8_t *byte);
/** @brief 清空接收环形缓冲和溢出计数 */
void USART1_ClearRxBuffer(void);
/** @brief 获取接收缓冲发生满溢的累计次数 */
uint16_t USART1_GetRxOverflowCount(void);
/** @brief 轮询读取并回显串口数据，保留作基础测试接口 */
void USART1_EchoTask(void);

#endif

