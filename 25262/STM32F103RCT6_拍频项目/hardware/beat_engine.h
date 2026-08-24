/**
  ******************************************************************************
  * @file    beat_engine.h
  * @brief   拍频信号产生、采样、求和与状态查询接口
  ******************************************************************************
  */

#ifndef __BEAT_ENGINE_H
#define __BEAT_ENGINE_H

#include "stm32f10x.h"
#include "beat_project_config.h"

/*
 * typedef enum 为枚举创建 BeatWaveformType 类型名，
 * 之后变量可以直接写 BeatWaveformType，而不必写 enum。
 */
typedef enum
{
    /* 正弦波。 */
    BEAT_WAVE_SINE = 0,
    /* 三角波。 */
    BEAT_WAVE_TRIANGLE,
    /* 锯齿波。 */
    BEAT_WAVE_SAW,
    /* 方波。 */
    BEAT_WAVE_SQUARE
} BeatWaveformType;

/*
 * typedef struct 把多项相关数据组合成一个配置对象；
 * 用户可以通过菜单调整并交给 BeatEngine 生效。
 */
typedef struct
{
    /* x1 波形类型。 */
    BeatWaveformType wave1;
    /* x2 波形类型。 */
    BeatWaveformType wave2;
    /* x1 幅度档位：1~11 对应 0.3~3.3V。 */
    uint8_t amplitude_step;
    /* x1 相位档位：0~12 对应 0~180 度。 */
    uint8_t phase_step;
    /* x1 相对 440Hz 的频差：-30~+30Hz。 */
    int8_t delta_hz;
} BeatConfig;

/* 数组长度在编译期由 BEAT_FRAME_SAMPLES 宏确定。 */
/* 该结构提供给显示层和串口导出，表示三路一致性快照。 */
typedef struct
{
    /* ADC 真实采样的 x1。 */
    uint16_t x1[BEAT_FRAME_SAMPLES];
    /* 软件实时生成的 x2。 */
    uint16_t x2[BEAT_FRAME_SAMPLES];
    /* 未缩放求和数据，范围可到 8190。 */
    uint16_t sum[BEAT_FRAME_SAMPLES];
    /* 当前数组中的有效点数。 */
    uint16_t sample_count;
    /* 发布该帧时的处理序号。 */
    uint32_t sequence;
} BeatFrame;

/* 该结构只保存状态副本，用于 USART 诊断和应用层超时判断。 */
typedef struct
{
    /* 已完成的 ADC DMA 半区数量。 */
    uint32_t frame_count;
    /* APB1 定时器实际输入时钟。 */
    uint32_t timer_clock_hz;
    /* TIM2 整数分频后得到的实际 ADC 采样率。 */
    uint32_t sample_rate_hz;
    /* 最近半区 x1 的最小/最大 ADC 码值。 */
    uint16_t adc_min;
    uint16_t adc_max;
    /* DAC2 缩放使用的 sum 保持峰值。 */
    uint16_t sum_peak;
    /* FIFO 当前有效样本数量。 */
    uint16_t fifo_count;
    /* ADC 软件触发自检结果。 */
    uint16_t adc_selftest_value;
    /* DMA1_CH1 当前剩余传输数量。 */
    uint16_t adc_dma_remaining;
    /* 两个定时器的实际 ARR。 */
    uint16_t tim2_arr;
    uint16_t tim6_arr;
    /* 是否已经处理过至少一个 ADC 半区。 */
    uint8_t frame_valid;
    /* ADC 单次软件触发自检是否成功。 */
    uint8_t adc_selftest_ok;
    /* TIM2_CC2 是否出现过触发标志。 */
    uint8_t tim2_cc2_seen;
} BeatEngineStatus;

/** @brief 初始化 DAC、ADC、TIM、DMA 和软件 FIFO */
void BeatEngine_Init(void);

/**
 * @brief  读取当前已生效的拍频参数
 * @param  config 用于接收参数的结构体指针
 */
void BeatEngine_GetConfig(BeatConfig *config);

/**
 * @brief  应用完整参数，并重新建立实时数据流
 * @param  config 待应用的参数
 */
void BeatEngine_ApplyConfig(const BeatConfig *config);

/**
 * @brief  单独调整 x1 与 440Hz 基准之间的频差
 * @param  delta_hz 目标频差，函数内部限制在 -30~+30Hz
 */
void BeatEngine_SetDeltaHz(int8_t delta_hz);

/**
 * @brief  从 FIFO 复制最新一帧 x1、x2 和 sum 数据
 * @param  frame 用于接收一致性快照的帧结构
 * @retval 1 复制成功
 * @retval 0 当前没有完整有效帧
 */
uint8_t BeatEngine_CopyLatestFrame(BeatFrame *frame);

/**
 * @brief  获取 ADC、DMA、定时器和 FIFO 的运行状态
 * @param  status 用于接收状态信息的结构体指针
 */
void BeatEngine_GetStatus(BeatEngineStatus *status);

#endif
