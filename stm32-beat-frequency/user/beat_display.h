/**
  ******************************************************************************
  * @file    beat_display.h
  * @brief   OLED/LCD 拍频显示后端统一接口
  ******************************************************************************
  */

#ifndef __BEAT_DISPLAY_H
#define __BEAT_DISPLAY_H

#include "beat_project_config.h"
#include "beat_engine.h"

/**
 * @brief  初始化当前宏选择的显示后端
 * @retval 1 初始化成功
 * @retval 0 初始化失败
 */
uint8_t BeatDisplay_Init(void);

/**
 * @brief  显示 x1、x2 和求和信号的波形页面
 * @param  config 当前拍频参数
 * @param  status ADC、DMA 和 FIFO 运行状态
 * @param  frame 最新一致性快照
 * @param  frame_valid 非零表示 frame 可以用于绘图
 */
void BeatDisplay_ShowWave(const BeatConfig *config,
                          const BeatEngineStatus *status,
                          const BeatFrame *frame,
                          uint8_t frame_valid);

/**
 * @brief  显示参数菜单及当前选中状态
 * @param  config 菜单中正在浏览或编辑的参数
 * @param  selected_item 当前选项，范围为 0~3
 * @param  editing 非零表示处于编辑状态
 */
void BeatDisplay_ShowMenu(const BeatConfig *config,
                          uint8_t selected_item,
                          uint8_t editing);

/** @brief LCD 示波器页切换当前控制项；OLED 后端为空操作 */
void BeatDisplay_ScopeNextControl(void);

/**
 * @brief  用编码器调整 LCD 示波器当前控制项
 * @retval 1 编码器已被示波器参数使用
 * @retval 0 当前仍由 BeatApp 调整频差 df
 */
uint8_t BeatDisplay_ScopeAdjust(int16_t steps);

/** @brief LCD 波形页切换 RUN/STOP；OLED 后端为空操作 */
void BeatDisplay_ScopeToggleRun(void);

/** @retval 非零表示显示层允许 BeatApp 更新最新帧 */
uint8_t BeatDisplay_ScopeIsRunning(void);

#endif
