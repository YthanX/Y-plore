/**
  ******************************************************************************
  * @file    oled_canvas.h
  * @brief   128x64 OLED 单色画布及整帧刷新接口
  ******************************************************************************
  */

#ifndef __OLED_CANVAS_H
#define __OLED_CANVAS_H

#include "stm32f10x.h"
#include "beat_chinese_font.h"

/** @brief 清空 8×128 单色帧缓冲 */
void OledCanvas_Clear(void);
/** @brief 设置或清除一个像素 */
void OledCanvas_SetPixel(uint8_t x, uint8_t y, uint8_t on);
/** @brief 使用整数算法绘制直线 */
void OledCanvas_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
/** @brief 绘制空心矩形 */
void OledCanvas_DrawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
/** @brief 绘制或清除实心矩形区域 */
void OledCanvas_FillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t on);
/** @brief 使用 5×7 ASCII 字模绘制字符串 */
void OledCanvas_DrawString(uint8_t x, uint8_t y, const char *text);
/** @brief 绘制一个项目专用 16×16 中文字模 */
void OledCanvas_DrawChineseGlyph(uint8_t x, uint8_t y, BeatChineseGlyph glyph);
/** @brief 对指定矩形区域执行像素反显 */
void OledCanvas_InvertRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
/** @brief 将完整画布按 8 个 page 刷新到 SSD1306 */
void OledCanvas_Flush(void);

#endif
