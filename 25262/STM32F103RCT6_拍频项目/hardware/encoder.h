/**
  ******************************************************************************
  * @file    encoder.h
  * @brief   TIM3 硬件编码器接口
  ******************************************************************************
  */

#ifndef __ENCODER_H
#define __ENCODER_H

#include "stm32f10x.h"

/** @brief 初始化 PA6/PA7 和 TIM3 Encoder mode */
void Encoder_Init(void);

/**
 * @brief  读取自上次调用以来的旋转档位变化
 * @retval 正数和负数代表两个方向，0 表示没有完整档位
 */
int16_t Encoder_GetDelta(void);

#endif
