/**
  ******************************************************************************
  * @file    beat_fifo.h
  * @brief   三路拍频样本的软件环形 FIFO
  ******************************************************************************
  */

#ifndef __BEAT_FIFO_H
#define __BEAT_FIFO_H

#include "stm32f10x.h"
#include "beat_project_config.h"

/*
 * typedef struct 创建 BeatSample 类型；
 * 它是 FIFO 的最小数据单元，三路值属于同一个 ADC 采样时刻。
 */
typedef struct
{
    /* ADC 回采值。 */
    uint16_t x1;
    /* 软件计算值。 */
    uint16_t x2;
    /* 未缩放求和。 */
    uint16_t sum;
} BeatSample;

/** @brief 清空 FIFO，并更新 generation 版本号 */
void BeatFifo_Reset(void);

/**
 * @brief  写入同一采样时刻的 x1、x2 和 sum
 * @param  sample 待写入的三路样本
 */
void BeatFifo_Push(const BeatSample *sample);

/**
 * @brief  按时间顺序复制最新若干组样本
 * @param  samples 目标数组
 * @param  count 请求复制的样本数量
 * @retval 实际复制数量；复制期间数据变化时返回 0
 */
uint16_t BeatFifo_CopyLatest(BeatSample *samples, uint16_t count);

/** @brief 获取 FIFO 当前有效样本数量 */
uint16_t BeatFifo_GetCount(void);

#endif
