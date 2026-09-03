/**
  ******************************************************************************
  * @file    beat_project_config.h
  * @brief   显示后端、采样配置和 x1 生成方式选择
  ******************************************************************************
  */

#ifndef __BEAT_PROJECT_CONFIG_H
#define __BEAT_PROJECT_CONFIG_H

#define BEAT_DISPLAY_LCD  1
#define BEAT_DISPLAY_OLED 2

#define BEAT_X1_MODE_TABLE 1
#define BEAT_X1_MODE_DDS   2

/* 修改这一行即可切换显示后端，同时会选中对应的采样率和 FIFO 容量。 */
#ifndef BEAT_DISPLAY_BACKEND
#define BEAT_DISPLAY_BACKEND BEAT_DISPLAY_LCD
#endif

/*
 * TABLE：第一版固定 256 点波表，便于观察定时器整数分频带来的频差。
 * DDS：第二版连续相位累加器，配合 DMA double-buffer 动态填充。
 */
#ifndef BEAT_X1_MODE
#define BEAT_X1_MODE BEAT_X1_MODE_DDS
#endif

#if (BEAT_X1_MODE != BEAT_X1_MODE_TABLE) && \
    (BEAT_X1_MODE != BEAT_X1_MODE_DDS)
#error Unsupported BEAT_X1_MODE
#endif

#if BEAT_X1_MODE == BEAT_X1_MODE_TABLE
#define BEAT_X1_MODE_NAME "TABLE"
#else
#define BEAT_X1_MODE_NAME "DDS"
#endif

#if BEAT_DISPLAY_BACKEND == BEAT_DISPLAY_LCD
#define BEAT_SAMPLE_RATE_HZ  16384U
#define BEAT_FRAME_SAMPLES   256U
#define BEAT_FIFO_CAPACITY   512U
#elif BEAT_DISPLAY_BACKEND == BEAT_DISPLAY_OLED
#define BEAT_SAMPLE_RATE_HZ  8192U
#define BEAT_FRAME_SAMPLES   128U
#define BEAT_FIFO_CAPACITY   256U
#else
#error Unsupported BEAT_DISPLAY_BACKEND
#endif

#endif
