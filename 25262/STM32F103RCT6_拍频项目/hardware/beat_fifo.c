/**
  ******************************************************************************
  * @file    beat_fifo.c
  * @brief   环形 FIFO 写入、回绕及一致性快照实现
  ******************************************************************************
  */

#include "beat_fifo.h"

/*
 * static 让 FIFO 存储区和索引只在本文件可见；
 * volatile 用于提醒编译器这些状态可能在 DMA 中断和主循环之间变化。
 */

/* 环形存储区；容量由 OLED/LCD 后端配置决定，且必须为 2 的幂。 */
static BeatSample s_samples[BEAT_FIFO_CAPACITY];
/* 指向“下一次写入位置”，不是最新样本所在位置。 */
static volatile uint16_t s_write_index;
/* 当前有效样本数，达到容量后保持不变。 */
static volatile uint16_t s_count;
/* 每次完整写入或 Reset 后递增，用于一致性复制校验。 */
static volatile uint32_t s_generation;

/**
 * @brief  清空 FIFO 的逻辑状态
 * @note   不需要逐字节清除 s_samples，因为 count=0 后旧内容不会被读取。
 */
void BeatFifo_Reset(void)
{
    /* 下一次从数组第 0 项开始写。 */
    s_write_index = 0;
    /* 没有任何有效样本。 */
    s_count = 0;
    /* Reset 同样属于数据版本变化，正在复制的消费者应放弃旧窗口。 */
    s_generation++;
}

/**
 * @brief  向环形 FIFO 写入一组三路同步样本
 * @param  sample 同一采样时刻的 x1、x2 和 sum
 */
void BeatFifo_Push(const BeatSample *sample)
{
    /* 防御空指针，避免中断中访问非法地址。 */
    if (sample == 0)
    {
        return;
    }

    /* 写满后从头覆盖最旧数据，write_index 始终指向下一次写入位置。 */
    s_samples[s_write_index] = *sample;
    /*
     * 容量为 2 的幂，按位与等价于对容量取模；
     * 到达数组末尾后索引自动回到 0。
     */
    s_write_index = (uint16_t)((s_write_index + 1U) & (BEAT_FIFO_CAPACITY - 1U));
    if (s_count < BEAT_FIFO_CAPACITY)
    {
        /* FIFO 未满时增加有效数量；写满后覆盖旧数据但 count 不再增加。 */
        s_count++;
    }
    /* 样本和索引全部更新后再增加版本号。 */
    s_generation++;
}

/**
 * @brief  复制 FIFO 中最新的 count 组样本
 * @param  samples 接收数组
 * @param  count 调用方希望获取的数量
 * @retval 成功时返回实际复制数；三次复制均被写入打断时返回 0
 */
uint16_t BeatFifo_CopyLatest(BeatSample *samples, uint16_t count)
{
    /* attempt 记录一致性复制已经尝试的次数。 */
    uint8_t attempt;

    /* 目标为空或请求数量为 0 时没有复制意义。 */
    if ((samples == 0) || (count == 0U))
    {
        return 0;
    }

    /*
     * 复制前后 generation 相同，说明期间没有新样本写入；
     * 若版本变化则丢弃本次结果，最多重试三次。
     */
    for (attempt = 0; attempt < 3U; attempt++)
    {
        /* 复制前记录数据版本。 */
        /* generation_before 为复制开始时的数据版本。 */
        uint32_t generation_before = s_generation;
        /* volatile 状态只读取一次，后续计算使用本地副本。 */
        /* available/copy_count 是有效数量和本轮实际复制数量。 */
        uint16_t available = s_count;
        /* FIFO 数据不足时只复制当前已有数量。 */
        uint16_t copy_count = count < available ? count : available;
        /*
         * write_index 是下一次写入位置，因此最新窗口要向前回退 copy_count。
         * 加上容量后再按位取模，可同时处理数组物理回绕。
         */
        /* start 是最新窗口中最旧一个样本的物理数组索引。 */
        uint16_t start = (uint16_t)((s_write_index + BEAT_FIFO_CAPACITY - copy_count) &
                                    (BEAT_FIFO_CAPACITY - 1U));
        /* i 遍历本次需要复制的样本。 */
        uint16_t i;

        for (i = 0; i < copy_count; i++)
        {
            /* 即使源地址越过数组末尾，目标数组仍按时间从旧到新连续排列。 */
            samples[i] = s_samples[(start + i) & (BEAT_FIFO_CAPACITY - 1U)];
        }

        if (generation_before == s_generation)
        {
            /* 前后版本相同，表示复制期间没有完整的新样本写入。 */
            return copy_count;
        }
        /* 版本不同则本次快照可能混入新旧窗口，放弃并重新复制。 */
    }

    /* 实时写入过于频繁时宁可返回失败，也不向显示层发布不一致数据。 */
    return 0;
}

/**
 * @brief  查询 FIFO 当前有效样本数量
 */
uint16_t BeatFifo_GetCount(void)
{
    return s_count;
}
