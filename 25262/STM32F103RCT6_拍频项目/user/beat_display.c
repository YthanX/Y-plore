/**
  ******************************************************************************
  * @file    beat_display.c
  * @brief   OLED 基线界面及 LCD 扩展界面的绘制实现
  ******************************************************************************
  */

#include "beat_display.h"

#include <stdio.h>

#include "beat_chinese_font.h"

/*
 * 本文件通过条件编译只保留一个显示后端。
 * static 函数和 static 缓冲均只在 beat_display.c 内部使用，
 * BeatApp 只能看到 BeatDisplay_Init/ShowWave/ShowMenu 三个公共 API。
 */

/** @brief 把频差格式化为固定宽度字符串，例如 DF:+10HZ */
static void BeatDisplay_FormatDelta(char text[9], int8_t delta)
{
    /* 先取绝对值，符号由 snprintf 单独写入。 */
    uint8_t value = delta < 0 ? (uint8_t)(-delta) : (uint8_t)delta;

    (void)snprintf(text, 9U, "DF:%c%02uHZ", delta < 0 ? '-' : '+',
                   (unsigned int)value);
}

/** @brief 把幅度档位转换为 0.3V~3.3V 文本 */
static void BeatDisplay_FormatAmplitude(char text[6], uint8_t step,
                                        uint8_t editing)
{
    /* 每档 0.3V，使用十分之一伏整数避免浮点格式化。 */
    uint8_t tenths = (uint8_t)(step * 3U);

    (void)snprintf(text, 6U, "%u.%uV%s", (unsigned int)(tenths / 10U),
                   (unsigned int)(tenths % 10U), editing != 0U ? "*" : "");
}

/** @brief 把相位档位转换为 0DEG~180DEG 文本 */
static void BeatDisplay_FormatPhase(char text[9], uint8_t phase_step,
                                    uint8_t editing)
{
    (void)snprintf(text, 9U, "%uDEG%s", (unsigned int)phase_step * 15U,
                   editing != 0U ? "*" : "");
}

/** @brief 把波形枚举转换成两个中文点阵字的索引 */
static void BeatDisplay_GetWaveGlyphs(BeatWaveformType wave,
                                      BeatChineseGlyph *first,
                                      BeatChineseGlyph *second)
{
    switch (wave)
    {
        case BEAT_WAVE_TRIANGLE:
            /* “三角”。 */
            *first = BEAT_CHINESE_SAN;
            *second = BEAT_CHINESE_JIAO;
            break;
        case BEAT_WAVE_SAW:
            /* “锯齿”。 */
            *first = BEAT_CHINESE_JU;
            *second = BEAT_CHINESE_CHI;
            break;
        case BEAT_WAVE_SQUARE:
            /* “方波”。 */
            *first = BEAT_CHINESE_FANG;
            *second = BEAT_CHINESE_BO;
            break;
        case BEAT_WAVE_SINE:
        default:
            /* 默认回退到“正弦”。 */
            *first = BEAT_CHINESE_ZHENG;
            *second = BEAT_CHINESE_XIAN;
            break;
    }
}

#if BEAT_DISPLAY_BACKEND == BEAT_DISPLAY_LCD

/* LCD 分支只在编译选择 LCD 后端时进入，不会与 OLED 同时占用引脚。 */
#include "LCD/Fonts/fonts.h"
#include "LCD/stm32_adafruit_lcd.h"

/* LCD 页面配色定义，RGB565 具体值由底层 LCD 头文件提供。 */
#define LCD_BG_COLOR       LCD_COLOR_BLACK
#define LCD_TEXT_COLOR     LCD_COLOR_WHITE
#define LCD_GRID_COLOR     LCD_COLOR_GRAY
#define LCD_X1_COLOR       LCD_COLOR_CYAN
#define LCD_X2_COLOR       LCD_COLOR_YELLOW
#define LCD_SUM_COLOR      LCD_COLOR_GREEN
#define LCD_SELECT_COLOR   LCD_COLOR_BLUE
#define LCD_EDIT_COLOR     LCD_COLOR_YELLOW
/* 三块波形区共用的左边界、像素宽度和内部高度。 */
#define LCD_PLOT_LEFT      38U
#define LCD_PLOT_WIDTH     279U
#define LCD_PLOT_HEIGHT    59U
/* 单通道示波器使用两块 tile 拼接，减少每帧需要发送到 LCD 的像素。 */
#define LCD_SCOPE_TOP      48U
#define LCD_SCOPE_HEIGHT   118U
#define LCD_SCOPE_XY_SIZE  174U
#define LCD_SCOPE_XY_LEFT  73U

typedef enum
{
    LCD_SCOPE_CTRL_DF = 0,
    LCD_SCOPE_CTRL_CHANNEL,
    LCD_SCOPE_CTRL_TIME,
    LCD_SCOPE_CTRL_GAIN,
    LCD_SCOPE_CTRL_MODE,
    LCD_SCOPE_CTRL_TRIGGER,
    LCD_SCOPE_CTRL_COUNT
} BeatLcdScopeControl;

typedef enum
{
    LCD_SCOPE_CHANNEL_ALL = 0,
    LCD_SCOPE_CHANNEL_X1,
    LCD_SCOPE_CHANNEL_X2,
    LCD_SCOPE_CHANNEL_SUM
} BeatLcdScopeChannel;

typedef enum
{
    /* 尚未绘制任何完整页面。 */
    LCD_PAGE_NONE = 0,
    /* 三路波形页。 */
    LCD_PAGE_WAVE,
    /* 中文参数菜单页。 */
    LCD_PAGE_MENU
} BeatLcdPage;

/* 局部 RGB565 缓冲只覆盖一个波形区，避免申请 320×240 整屏内存。 */
static uint16_t s_lcd_pixels[LCD_PLOT_WIDTH * LCD_PLOT_HEIGHT];
/* 暂存每列波形的 y 坐标，第二遍绘制时连接相邻列。 */
static uint8_t s_wave_y[LCD_PLOT_WIDTH];
/* X-Y 模式额外保存横坐标，纵坐标继续复用 s_wave_y。 */
static uint8_t s_wave_x[BEAT_FRAME_SAMPLES];
/* 当前页面以及标题区缓存，用于减少不必要的整屏刷新。 */
static BeatLcdPage s_lcd_page;
static int8_t s_header_delta;
static uint8_t s_header_adc_state;
/* LCD 示波器控制状态：默认保持原三路页面和 df 编码器操作。 */
static uint8_t s_scope_control = LCD_SCOPE_CTRL_DF;
static uint8_t s_scope_channel = LCD_SCOPE_CHANNEL_ALL;
static uint8_t s_scope_time_zoom;
/* 显示增益共五档，索引 2 对应默认 X1。 */
static uint8_t s_scope_gain = 2U;
static uint8_t s_scope_xy_mode;
static uint8_t s_scope_trigger;
static uint8_t s_scope_running = 1U;
static uint8_t s_scope_header_dirty = 1U;
static uint8_t s_scope_redraw_requested = 1U;

/** @brief 同时设置 LCD 前景色和背景色 */
static void BeatLcd_SetColors(uint16_t foreground, uint16_t background)
{
    BSP_LCD_SetTextColor(foreground);
    BSP_LCD_SetBackColor(background);
}

/** @brief 把带符号的枚举值循环限制到 0~limit-1 */
static uint8_t BeatLcd_WrapValue(uint8_t value, int16_t steps, uint8_t limit)
{
    int16_t result = (int16_t)value + steps;

    while (result < 0)
    {
        result += limit;
    }
    while (result >= limit)
    {
        result -= limit;
    }
    return (uint8_t)result;
}

void BeatDisplay_ScopeNextControl(void)
{
    s_scope_control = (uint8_t)((s_scope_control + 1U) % LCD_SCOPE_CTRL_COUNT);
    s_scope_header_dirty = 1U;
}

uint8_t BeatDisplay_ScopeAdjust(int16_t steps)
{
    if (s_scope_control == LCD_SCOPE_CTRL_DF)
    {
        /* DF 仍由 BeatApp 调用 BeatEngine_SetDeltaHz()，保持原作业操作。 */
        return 0U;
    }
    if (steps == 0)
    {
        return 1U;
    }

    if (s_scope_control == LCD_SCOPE_CTRL_CHANNEL)
    {
        /* Y-T 选择显示通道；X-Y 选择两路信号的坐标配对。 */
        s_scope_channel = BeatLcd_WrapValue(s_scope_channel, steps,
                                            s_scope_xy_mode != 0U ? 3U : 4U);
    }
    else if (s_scope_control == LCD_SCOPE_CTRL_TIME)
    {
        s_scope_time_zoom = BeatLcd_WrapValue(s_scope_time_zoom, steps, 3U);
    }
    else if (s_scope_control == LCD_SCOPE_CTRL_GAIN)
    {
        s_scope_gain = BeatLcd_WrapValue(s_scope_gain, steps, 5U);
    }
    else if (s_scope_control == LCD_SCOPE_CTRL_MODE)
    {
        s_scope_xy_mode = BeatLcd_WrapValue(s_scope_xy_mode, steps, 2U);
        /* X-Y 只有三组配对，原来停在 Y-T 的 X 通道时回到第一组。 */
        if ((s_scope_xy_mode != 0U) && (s_scope_channel >= 3U))
        {
            s_scope_channel = 0U;
        }
    }
    else
    {
        s_scope_trigger = BeatLcd_WrapValue(s_scope_trigger, steps, 3U);
    }

    /* 参数变化后重新建立页面布局，并用当前帧立即重画。 */
    s_lcd_page = LCD_PAGE_NONE;
    s_scope_header_dirty = 1U;
    s_scope_redraw_requested = 1U;
    return 1U;
}

void BeatDisplay_ScopeToggleRun(void)
{
    s_scope_running = s_scope_running == 0U ? 1U : 0U;
    s_scope_header_dirty = 1U;
    if (s_scope_running != 0U)
    {
        s_scope_redraw_requested = 1U;
    }
}

uint8_t BeatDisplay_ScopeIsRunning(void)
{
    return s_scope_running;
}

/** @brief 在指定坐标使用当前字体绘制 ASCII 文本 */
static void BeatLcd_DrawText(uint16_t x, uint16_t y, const char *text)
{
    BSP_LCD_DisplayStringAt(x, y, (uint8_t *)text, LEFT_MODE);
}

/** @brief 围绕通道中值应用 X1/4~X4 显示增益，不改变原始数据 */
static uint16_t BeatLcd_ApplyGain(uint16_t value, uint16_t scale)
{
    int32_t center = (int32_t)((scale + 1U) / 2U);
    int32_t delta = (int32_t)value - center;
    int32_t adjusted;

    if (s_scope_gain == 0U)
    {
        delta /= 4;
    }
    else if (s_scope_gain == 1U)
    {
        delta /= 2;
    }
    else if (s_scope_gain == 3U)
    {
        delta *= 2;
    }
    else if (s_scope_gain == 4U)
    {
        delta *= 4;
    }
    adjusted = center + delta;

    if (adjusted < 0)
    {
        adjusted = 0;
    }
    if (adjusted > scale)
    {
        adjusted = scale;
    }
    return (uint16_t)adjusted;
}

/** @brief 根据时域缩放选择本屏显示 256/128/64 个最新点 */
static uint16_t BeatLcd_GetVisibleCount(uint16_t count)
{
    uint16_t visible = (uint16_t)(count >> s_scope_time_zoom);

    if (visible < 2U)
    {
        visible = count < 2U ? count : 2U;
    }
    return visible;
}

/** @brief 在选定通道中寻找中线的上升沿或下降沿，并返回显示起点 */
static uint16_t BeatLcd_FindTriggerStart(const uint16_t *samples,
                                        uint16_t count, uint16_t visible,
                                        uint16_t scale)
{
    uint16_t latest_start;
    uint16_t pretrigger;
    int32_t index;
    int32_t minimum_index;
    int32_t maximum_index;
    uint16_t level = (uint16_t)((scale + 1U) / 2U);

    if ((samples == 0) || (visible >= count) || (s_scope_trigger == 0U))
    {
        return count > visible ? (uint16_t)(count - visible) : 0U;
    }

    latest_start = (uint16_t)(count - visible);
    pretrigger = (uint16_t)(visible / 4U);
    minimum_index = pretrigger > 0U ? pretrigger : 1;
    maximum_index = (int32_t)latest_start + pretrigger;

    /* 从最新位置向前找，使触发点尽量靠近屏幕左侧四分之一处。 */
    for (index = maximum_index; index >= minimum_index; index--)
    {
        uint16_t previous = samples[index - 1];
        uint16_t current = samples[index];
        uint8_t crossing = s_scope_trigger == 1U ?
                           (uint8_t)((previous < level) && (current >= level)) :
                           (uint8_t)((previous > level) && (current <= level));

        if (crossing != 0U)
        {
            return (uint16_t)(index - pretrigger);
        }
    }
    return latest_start;
}

/** @brief 把 16×16 中文字模放大为 32×32 RGB565 图块 */
static void BeatLcd_DrawChinese(uint16_t x, uint16_t y,
                                BeatChineseGlyph glyph,
                                uint16_t foreground, uint16_t background)
{
    /* source_x/source_y 遍历 16×16 源点阵。 */
    uint8_t source_y;
    uint8_t source_x;

    /* 每个源像素横向、纵向各复制 2 次，实现整数倍放大。 */
    for (source_y = 0U; source_y < 16U; source_y++)
    {
        /* 字库每行使用 16 位，高位对应左侧像素。 */
        uint16_t row = BeatChineseFont_GetRow(glyph, source_y);
        /* target_y 表示当前源行复制到放大图中的第 0/1 行。 */
        uint8_t target_y;

        for (target_y = 0U; target_y < 2U; target_y++)
        {
            for (source_x = 0U; source_x < 16U; source_x++)
            {
                /* 点阵位为 1 使用前景色，否则写背景色。 */
                uint16_t color = (row & (uint16_t)(1U << (15U - source_x))) != 0U ?
                                 foreground : background;
                /* index 是 32×32 局部 RGB565 缓冲中的线性下标。 */
                uint16_t index = (uint16_t)((source_y * 2U + target_y) * 32U +
                                            source_x * 2U);
                s_lcd_pixels[index] = color;
                s_lcd_pixels[index + 1U] = color;
            }
        }
    }
    /* 一次批量写出图块，减少逐像素 GPIO 调用。 */
    BSP_LCD_DrawRGB16Image(x, y, 32U, 32U, s_lcd_pixels);
}

/** @brief 连续绘制两个 32×32 中文字 */
static void BeatLcd_DrawChinesePair(uint16_t x, uint16_t y,
                                    BeatChineseGlyph first,
                                    BeatChineseGlyph second,
                                    uint16_t foreground, uint16_t background)
{
    BeatLcd_DrawChinese(x, y, first, foreground, background);
    BeatLcd_DrawChinese((uint16_t)(x + 32U), y, second, foreground, background);
}

/** @brief 绘制一块 LCD 波形区域的标签和固定边框 */
static void BeatLcd_DrawWaveFrame(uint16_t top, uint16_t bottom,
                                  const char *label)
{
    BSP_LCD_SetFont(&Font16);
    BeatLcd_SetColors(LCD_TEXT_COLOR, LCD_BG_COLOR);
    BeatLcd_DrawText(4U, (uint16_t)(top + 21U), label);
    BSP_LCD_SetTextColor(LCD_GRID_COLOR);
    BSP_LCD_DrawRect(36U, top, 282U, (uint16_t)(bottom - top + 1U));
}

/** @brief 将一路采样数组映射并批量绘制到 LCD 局部区域 */
static void BeatLcd_RenderWave(const uint16_t *samples, uint16_t count,
                               uint16_t start_index, uint16_t scale,
                               uint16_t top, uint16_t color, uint8_t apply_gain)
{
    /* pixel_count 用于清缓冲，pixel/column 分别遍历像素和屏幕列。 */
    uint32_t pixel_count = (uint32_t)LCD_PLOT_WIDTH * LCD_PLOT_HEIGHT;
    uint32_t pixel;
    uint16_t column;
    /* sample_span 是从 start_index 到帧尾的有效源样本数量。 */
    uint16_t sample_span;

    /* LCD 不建立整屏 Frame Buffer，只准备当前一块波形区域。 */
    for (pixel = 0U; pixel < pixel_count; pixel++)
    {
        s_lcd_pixels[pixel] = LCD_BG_COLOR;
    }
    for (column = 0U; column < LCD_PLOT_WIDTH; column++)
    {
        s_lcd_pixels[(LCD_PLOT_HEIGHT / 2U) * LCD_PLOT_WIDTH + column] =
            LCD_GRID_COLOR;
    }

    if ((samples == 0) || (count == 0U) || (start_index >= count) ||
        (scale == 0U))
    {
        /* WAIT ADC 状态只发送黑底和中线，不访问 frame 数组。 */
        BSP_LCD_DrawRGB16Image(LCD_PLOT_LEFT, (uint16_t)(top + 2U),
                               LCD_PLOT_WIDTH, LCD_PLOT_HEIGHT, s_lcd_pixels);
        return;
    }

    /* 将输入样本横向重采样到固定 279 列，再映射到区域高度。 */
    sample_span = (uint16_t)(count - start_index);
    /* 相邻列补竖线，避免离散采样点之间出现明显断口。 */
    /* 固定画出区域中线，无数据时也保留基线。 */
    for (column = 0U; column < LCD_PLOT_WIDTH; column++)
    {
        /* index 是当前 LCD 列映射到的源采样数组下标。 */
        uint16_t index = (uint16_t)(start_index +
                         ((uint32_t)column * (sample_span - 1U)) /
                         (LCD_PLOT_WIDTH - 1U));
        /* 超量程样本先钳位，防止计算出的 y 坐标越界。 */
        uint16_t value = samples[index] > scale ? scale : samples[index];

        if (apply_gain != 0U)
        {
            value = BeatLcd_ApplyGain(value, scale);
        }

        /* 数值 0 映射到底部，scale 映射到顶部。 */
        s_wave_y[column] = (uint8_t)((LCD_PLOT_HEIGHT - 1U) -
                             (((uint32_t)value * (LCD_PLOT_HEIGHT - 1U) +
                               scale / 2U) / scale));
    }
    for (column = 0U; column < LCD_PLOT_WIDTH; column++)
    {
        /* from/to 是相邻两列 y，y_min/y_max 决定本列补线范围。 */
        uint8_t from = column == 0U ? s_wave_y[column] : s_wave_y[column - 1U];
        uint8_t to = s_wave_y[column];
        uint8_t y_min = from < to ? from : to;
        uint8_t y_max = from > to ? from : to;
        uint8_t y;

        /* 当前列连接上一列 y 和本列 y，形成连续折线。 */
        for (y = y_min; y <= y_max; y++)
        {
            s_lcd_pixels[(uint16_t)y * LCD_PLOT_WIDTH + column] = color;
        }
    }
    /* 整块波形区域一次写入 LCD。 */
    BSP_LCD_DrawRGB16Image(LCD_PLOT_LEFT, (uint16_t)(top + 2U),
                           LCD_PLOT_WIDTH, LCD_PLOT_HEIGHT, s_lcd_pixels);
}

/** @brief 用两个 59 行 tile 拼出 279×118 的单通道 Y-T 绘图区 */
static void BeatLcd_RenderScopeYt(const uint16_t *samples, uint16_t count,
                                  uint16_t start_index, uint16_t scale,
                                  uint16_t color)
{
    uint16_t column;
    uint16_t sample_span = count > start_index ? (uint16_t)(count - start_index) : 0U;
    uint16_t tile_start;

    if ((samples != 0) && (sample_span >= 2U))
    {
        /* 先计算整块示波区域中每一列对应的全局 y 坐标。 */
        for (column = 0U; column < LCD_PLOT_WIDTH; column++)
        {
            uint16_t index = (uint16_t)(start_index +
                             ((uint32_t)column * (sample_span - 1U)) /
                             (LCD_PLOT_WIDTH - 1U));
            uint16_t value = samples[index] > scale ? scale : samples[index];

            value = BeatLcd_ApplyGain(value, scale);
            s_wave_y[column] = (uint8_t)((LCD_SCOPE_HEIGHT - 1U) -
                                 (((uint32_t)value * (LCD_SCOPE_HEIGHT - 1U) +
                                   scale / 2U) / scale));
        }
    }

    /* 一次只在 RAM 中准备最多 59 行，然后立即批量写到 LCD。 */
    for (tile_start = 0U; tile_start < LCD_SCOPE_HEIGHT;
         tile_start = (uint16_t)(tile_start + LCD_PLOT_HEIGHT))
    {
        uint16_t tile_height = (uint16_t)(LCD_SCOPE_HEIGHT - tile_start);
        uint16_t local_y;

        if (tile_height > LCD_PLOT_HEIGHT)
        {
            tile_height = LCD_PLOT_HEIGHT;
        }

        for (local_y = 0U; local_y < tile_height; local_y++)
        {
            uint16_t global_y = (uint16_t)(tile_start + local_y);

            for (column = 0U; column < LCD_PLOT_WIDTH; column++)
            {
                uint8_t grid = (uint8_t)(((column % 28U) == 0U) ||
                                         ((global_y % 29U) == 0U) ||
                                         (global_y == LCD_SCOPE_HEIGHT / 2U));
                s_lcd_pixels[(uint32_t)local_y * LCD_PLOT_WIDTH + column] =
                    grid != 0U ? LCD_GRID_COLOR : LCD_BG_COLOR;
            }
        }

        if ((samples != 0) && (sample_span >= 2U))
        {
            for (column = 0U; column < LCD_PLOT_WIDTH; column++)
            {
                uint8_t from = column == 0U ? s_wave_y[column] :
                                               s_wave_y[column - 1U];
                uint8_t to = s_wave_y[column];
                uint8_t y_min = from < to ? from : to;
                uint8_t y_max = from > to ? from : to;
                uint16_t y;

                for (y = y_min; y <= y_max; y++)
                {
                    if ((y >= tile_start) &&
                        (y < (uint16_t)(tile_start + tile_height)))
                    {
                        s_lcd_pixels[(uint32_t)(y - tile_start) * LCD_PLOT_WIDTH +
                                     column] = color;
                    }
                }
            }
        }

        BSP_LCD_DrawRGB16Image(LCD_PLOT_LEFT,
                               (uint16_t)(LCD_SCOPE_TOP + tile_start),
                               LCD_PLOT_WIDTH, tile_height, s_lcd_pixels);
    }
}

/** @brief 在当前 X-Y tile 中连接两个采样点，超出 tile 的像素直接裁剪 */
static void BeatLcd_DrawXyLine(uint8_t x0, uint8_t y0,
                               uint8_t x1, uint8_t y1,
                               uint16_t tile_start, uint16_t tile_height)
{
    int16_t x = x0;
    int16_t y = y0;
    int16_t dx = x1 >= x0 ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy_abs = y1 >= y0 ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    int16_t dy = (int16_t)(-dy_abs);
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t error = (int16_t)(dx + dy);

    for (;;)
    {
        if ((y >= (int16_t)tile_start) &&
            (y < (int16_t)(tile_start + tile_height)))
        {
            s_lcd_pixels[(uint32_t)(y - (int16_t)tile_start) *
                         LCD_SCOPE_XY_SIZE + (uint16_t)x] = LCD_X1_COLOR;
        }
        if ((x == (int16_t)x1) && (y == (int16_t)y1))
        {
            break;
        }
        {
            int16_t twice_error = (int16_t)(2 * error);

            if (twice_error >= dy)
            {
                error = (int16_t)(error + dy);
                x = (int16_t)(x + sx);
            }
            if (twice_error <= dx)
            {
                error = (int16_t)(error + dx);
                y = (int16_t)(y + sy);
            }
        }
    }
}

/** @brief X-Y 模式：X1 决定横坐标，X2 决定纵坐标 */
static void BeatLcd_RenderScopeXy(const BeatFrame *frame, uint8_t frame_valid,
                                  uint16_t start_index)
{
    const uint16_t *x_samples = frame->x1;
    const uint16_t *y_samples = frame->x2;
    uint16_t x_scale = 4095U;
    uint16_t y_scale = 4095U;
    uint16_t tile_start;
    uint16_t count = frame_valid != 0U ? frame->sample_count : 0U;
    uint16_t point_count = 0U;

    /* PAIR 依次为 X1-X2、X1-X、X2-X。 */
    if (s_scope_channel == 1U)
    {
        y_samples = frame->sum;
        y_scale = 8190U;
    }
    else if (s_scope_channel == 2U)
    {
        x_samples = frame->x2;
        y_samples = frame->sum;
        y_scale = 8190U;
    }

    if ((frame_valid != 0U) && (start_index < count))
    {
        uint16_t i;

        point_count = (uint16_t)(count - start_index);
        if (point_count > BEAT_FRAME_SAMPLES)
        {
            point_count = BEAT_FRAME_SAMPLES;
        }
        for (i = 0U; i < point_count; i++)
        {
            uint16_t source = (uint16_t)(start_index + i);
            uint16_t x_value = BeatLcd_ApplyGain(x_samples[source], x_scale);
            uint16_t y_value = BeatLcd_ApplyGain(y_samples[source], y_scale);

            s_wave_x[i] = (uint8_t)(((uint32_t)x_value *
                                     (LCD_SCOPE_XY_SIZE - 1U)) / x_scale);
            s_wave_y[i] = (uint8_t)((LCD_SCOPE_XY_SIZE - 1U) -
                            ((uint32_t)y_value *
                             (LCD_SCOPE_XY_SIZE - 1U)) / y_scale);
        }
    }

    for (tile_start = 0U; tile_start < LCD_SCOPE_XY_SIZE;
         tile_start = (uint16_t)(tile_start + LCD_PLOT_HEIGHT))
    {
        uint16_t tile_height = (uint16_t)(LCD_SCOPE_XY_SIZE - tile_start);
        uint16_t local_y;
        uint16_t x;

        if (tile_height > LCD_PLOT_HEIGHT)
        {
            tile_height = LCD_PLOT_HEIGHT;
        }

        for (local_y = 0U; local_y < tile_height; local_y++)
        {
            uint16_t global_y = (uint16_t)(tile_start + local_y);

            for (x = 0U; x < LCD_SCOPE_XY_SIZE; x++)
            {
                uint8_t grid = (uint8_t)(((x % 29U) == 0U) ||
                                         ((global_y % 29U) == 0U) ||
                                         (x == LCD_SCOPE_XY_SIZE / 2U) ||
                                         (global_y == LCD_SCOPE_XY_SIZE / 2U));
                s_lcd_pixels[(uint32_t)local_y * LCD_SCOPE_XY_SIZE + x] =
                    grid != 0U ? LCD_GRID_COLOR : LCD_BG_COLOR;
            }
        }

        if (point_count >= 2U)
        {
            uint16_t i;

            /* 相邻采样点使用整数直线连接，XY 图不再只是离散散点。 */
            for (i = 1U; i < point_count; i++)
            {
                BeatLcd_DrawXyLine(s_wave_x[i - 1U], s_wave_y[i - 1U],
                                   s_wave_x[i], s_wave_y[i],
                                   tile_start, tile_height);
            }
        }

        BSP_LCD_DrawRGB16Image(LCD_SCOPE_XY_LEFT,
                               (uint16_t)(LCD_SCOPE_TOP + tile_start),
                               LCD_SCOPE_XY_SIZE, tile_height, s_lcd_pixels);
    }
}

/** @brief 更新 LCD 顶部标题、频差和 ADC 状态 */
static void BeatLcd_DrawHeader(const BeatConfig *config, uint8_t adc_ready)
{
    /* 0 表示三通道同屏，不把它误写成一个名为 ALL 的通道。 */
    static const char *channel_names[] = {"3CH", "X1", "X2", "X"};
    static const char *pair_names[] = {"X1-X2", "X1-X", "X2-X"};
    static const char *control_names[] = {"DF", "CH", "TIME", "GAIN", "MODE", "TRIG"};
    static const char *trigger_names[] = {"A", "R", "F"};
    static const char *gain_names[] = {"X1/4", "X1/2", "X1", "X2", "X4"};
    char delta_text[9];
    char line1[40];
    char line2[48];

    BeatDisplay_FormatDelta(delta_text, config->delta_hz);
    if (s_scope_xy_mode != 0U)
    {
        (void)snprintf(line1, sizeof(line1), "SCOPE %s PAIR:%s %s %s",
                       s_scope_running != 0U ? "RUN" : "STOP",
                       pair_names[s_scope_channel], delta_text,
                       adc_ready != 0U ? "ADC" : "WAIT");
    }
    else
    {
        (void)snprintf(line1, sizeof(line1), "SCOPE %s CH:%s %s %s",
                       s_scope_running != 0U ? "RUN" : "STOP",
                       channel_names[s_scope_channel], delta_text,
                       adc_ready != 0U ? "ADC" : "WAIT");
    }
    (void)snprintf(line2, sizeof(line2), "SEL:%s T:X%u G:%s %s TR:%s",
                   ((s_scope_xy_mode != 0U) &&
                    (s_scope_control == LCD_SCOPE_CTRL_CHANNEL)) ?
                       "PAIR" : control_names[s_scope_control],
                   (unsigned int)(1U << s_scope_time_zoom),
                   gain_names[s_scope_gain],
                   s_scope_xy_mode != 0U ? "XY" : "YT",
                   trigger_names[s_scope_trigger]);

    /* 标题只有两行，状态改变时才清除并重画。 */
    BSP_LCD_SetTextColor(LCD_BG_COLOR);
    BSP_LCD_FillRect(0U, 0U, 320U, 32U);
    BSP_LCD_SetFont(&Font12);
    BeatLcd_SetColors(LCD_TEXT_COLOR, LCD_BG_COLOR);
    BeatLcd_DrawText(4U, 2U, line1);
    BeatLcd_SetColors(LCD_EDIT_COLOR, LCD_BG_COLOR);
    BeatLcd_DrawText(4U, 17U, line2);
    s_header_delta = config->delta_hz;
    s_header_adc_state = adc_ready;
    s_scope_header_dirty = 0U;
}

/** @brief 初始化 ILI9341 LCD 后端 */
uint8_t BeatDisplay_Init(void)
{
    /* 底层初始化失败时向应用层返回 0。 */
    if (BSP_LCD_Init() != LCD_OK)
    {
        return 0U;
    }
    BSP_LCD_Clear(LCD_BG_COLOR);
    BSP_LCD_SetFont(&Font16);
    BeatLcd_SetColors(LCD_TEXT_COLOR, LCD_BG_COLOR);
    /* 使用非法缓存值，保证第一次 ShowWave 完整绘制。 */
    s_lcd_page = LCD_PAGE_NONE;
    s_header_delta = 127;
    s_header_adc_state = 2U;
    return 1U;
}

/** @brief 显示 LCD 三路波形页 */
void BeatDisplay_ShowWave(const BeatConfig *config,
                          const BeatEngineStatus *status,
                          const BeatFrame *frame,
                          uint8_t frame_valid)
{
    const uint16_t *selected_samples = 0;
    uint16_t selected_scale = 4095U;
    uint16_t selected_color = LCD_X1_COLOR;
    uint16_t visible = frame_valid != 0U ?
                       BeatLcd_GetVisibleCount(frame->sample_count) : 0U;
    uint16_t start_index = 0U;
    uint8_t page_changed = s_lcd_page != LCD_PAGE_WAVE ? 1U : 0U;

    (void)status;

    /* 先选择当前单通道的数据、量程和颜色。 */
    if (frame_valid != 0U)
    {
        if (s_scope_channel == LCD_SCOPE_CHANNEL_X2)
        {
            selected_samples = frame->x2;
            selected_color = LCD_X2_COLOR;
        }
        else if (s_scope_channel == LCD_SCOPE_CHANNEL_SUM)
        {
            selected_samples = frame->sum;
            selected_scale = 8190U;
            selected_color = LCD_SUM_COLOR;
        }
        else
        {
            selected_samples = frame->x1;
        }
    }

    /* 页面或布局变化时，只重画一次固定边框。 */
    if (page_changed != 0U)
    {
        BSP_LCD_Clear(LCD_BG_COLOR);
        s_lcd_page = LCD_PAGE_WAVE;
        s_header_delta = 127;
        s_header_adc_state = 2U;
        s_scope_header_dirty = 1U;
        s_scope_redraw_requested = 1U;

        if (s_scope_xy_mode != 0U)
        {
            static const char *pair_x_names[] = {"X1", "X1", "X2"};
            static const char *pair_y_names[] = {"X2", "X", "X"};
            char x_axis_text[8];
            char y_axis_text[8];

            BSP_LCD_SetTextColor(LCD_GRID_COLOR);
            BSP_LCD_DrawRect((uint16_t)(LCD_SCOPE_XY_LEFT - 2U),
                             (uint16_t)(LCD_SCOPE_TOP - 2U),
                             (uint16_t)(LCD_SCOPE_XY_SIZE + 4U),
                             (uint16_t)(LCD_SCOPE_XY_SIZE + 4U));
            BSP_LCD_SetFont(&Font12);
            BeatLcd_SetColors(LCD_X1_COLOR, LCD_BG_COLOR);
            (void)snprintf(x_axis_text, sizeof(x_axis_text), "X:%s",
                           pair_x_names[s_scope_channel]);
            BeatLcd_DrawText(276U, 214U, x_axis_text);
            BeatLcd_SetColors(LCD_X2_COLOR, LCD_BG_COLOR);
            (void)snprintf(y_axis_text, sizeof(y_axis_text), "Y:%s",
                           pair_y_names[s_scope_channel]);
            BeatLcd_DrawText(8U, 48U, y_axis_text);
        }
        else if (s_scope_channel == LCD_SCOPE_CHANNEL_ALL)
        {
            BeatLcd_DrawWaveFrame(36U, 98U, "X1");
            BeatLcd_DrawWaveFrame(104U, 166U, "X2");
            BeatLcd_DrawWaveFrame(172U, 234U, "X");
        }
        else
        {
            BSP_LCD_SetTextColor(LCD_GRID_COLOR);
            BSP_LCD_DrawRect((uint16_t)(LCD_PLOT_LEFT - 2U),
                             (uint16_t)(LCD_SCOPE_TOP - 2U),
                             (uint16_t)(LCD_PLOT_WIDTH + 4U),
                             (uint16_t)(LCD_SCOPE_HEIGHT + 4U));
        }
    }

    /* 标题只在参数、运行状态、频差或 ADC 状态变化时重画。 */
    if ((s_header_delta != config->delta_hz) ||
        (s_header_adc_state != frame_valid) ||
        (s_scope_header_dirty != 0U))
    {
        BeatLcd_DrawHeader(config, frame_valid);
    }

    /* STOP 时保留当前画面；控制项变化仍允许用冻结帧重新缩放。 */
    if ((s_scope_running == 0U) && (s_scope_redraw_requested == 0U))
    {
        return;
    }

    if (s_scope_xy_mode != 0U)
    {
        start_index = (frame_valid != 0U) ?
                      (uint16_t)(frame->sample_count - visible) : 0U;
        BeatLcd_RenderScopeXy(frame, frame_valid, start_index);
    }
    else
    {
        /* ALL 模式使用 X1 作为公共触发源，保证三路时间位置一致。 */
        const uint16_t *trigger_samples = s_scope_channel == LCD_SCOPE_CHANNEL_ALL ?
                                          (frame_valid != 0U ? frame->x1 : 0) :
                                          selected_samples;
        uint16_t trigger_scale = s_scope_channel == LCD_SCOPE_CHANNEL_SUM ?
                                 8190U : 4095U;

        start_index = frame_valid != 0U ?
                      BeatLcd_FindTriggerStart(trigger_samples,
                                               frame->sample_count,
                                               visible, trigger_scale) : 0U;

        if (s_scope_channel == LCD_SCOPE_CHANNEL_ALL)
        {
            BeatLcd_RenderWave(frame_valid != 0U ? frame->x1 : 0,
                               frame_valid != 0U ? frame->sample_count : 0U,
                               start_index, 4095U, 36U, LCD_X1_COLOR, 1U);
            BeatLcd_RenderWave(frame_valid != 0U ? frame->x2 : 0,
                               frame_valid != 0U ? frame->sample_count : 0U,
                               start_index, 4095U, 104U, LCD_X2_COLOR, 1U);
            BeatLcd_RenderWave(frame_valid != 0U ? frame->sum : 0,
                               frame_valid != 0U ? frame->sample_count : 0U,
                               start_index, 8190U, 172U, LCD_SUM_COLOR, 1U);
        }
        else
        {
            BeatLcd_RenderScopeYt(selected_samples,
                                  frame_valid != 0U ? frame->sample_count : 0U,
                                  start_index, selected_scale, selected_color);
        }
    }

    s_scope_redraw_requested = 0U;
}

/** @brief 绘制 LCD 菜单中的 ASCII 参数单元格 */
static void BeatLcd_DrawMenuCell(uint8_t item, uint8_t selected_item,
                                 uint16_t x, uint16_t y, uint16_t width,
                                 const char *text, uint8_t editing)
{
    /* 当前项使用强调背景，普通项使用页面背景。 */
    uint16_t background = item == selected_item ? LCD_SELECT_COLOR : LCD_BG_COLOR;

    BSP_LCD_SetTextColor(background);
    BSP_LCD_FillRect(x, y, width, 44U);
    BeatLcd_SetColors(LCD_TEXT_COLOR, background);
    BeatLcd_DrawText((uint16_t)(x + 10U), (uint16_t)(y + 12U), text);
    /* 选中且正在编辑时，在单元格右侧显示星号。 */
    if ((item == selected_item) && (editing != 0U))
    {
        BeatLcd_SetColors(LCD_EDIT_COLOR, background);
        BeatLcd_DrawText((uint16_t)(x + width - 16U), (uint16_t)(y + 12U), "*");
    }
}

/** @brief 绘制 LCD 菜单中的中文波形单元格 */
static void BeatLcd_DrawWaveCell(uint8_t item, uint8_t selected_item,
                                 uint16_t x, uint16_t y,
                                 BeatWaveformType wave, uint8_t editing)
{
    /* first/second 保存波形名称对应的两个中文字模枚举。 */
    BeatChineseGlyph first;
    BeatChineseGlyph second;
    uint16_t background = item == selected_item ? LCD_SELECT_COLOR : LCD_BG_COLOR;

    /* 把波形枚举转换为两个中文点阵。 */
    BeatDisplay_GetWaveGlyphs(wave, &first, &second);
    BSP_LCD_SetTextColor(background);
    BSP_LCD_FillRect(x, y, 104U, 44U);
    BeatLcd_DrawChinesePair((uint16_t)(x + 8U), (uint16_t)(y + 6U), first, second,
                            LCD_TEXT_COLOR, background);
    if ((item == selected_item) && (editing != 0U))
    {
        BeatLcd_SetColors(LCD_EDIT_COLOR, background);
        BeatLcd_DrawText((uint16_t)(x + 84U), (uint16_t)(y + 14U), "*");
    }
}

/** @brief 显示 LCD 两列三行参数菜单 */
void BeatDisplay_ShowMenu(const BeatConfig *config,
                          uint8_t selected_item,
                          uint8_t editing)
{
    /* amplitude/phase 是菜单中显示的 ASCII 参数字符串。 */
    char amplitude[6];
    char phase[9];

    /* 从波形页首次进入菜单时清整屏，后续只重画单元格。 */
    if (s_lcd_page != LCD_PAGE_MENU)
    {
        BSP_LCD_Clear(LCD_BG_COLOR);
        s_lcd_page = LCD_PAGE_MENU;
    }
    BSP_LCD_SetFont(&Font16);
    /* LCD 星号单独绘制，因此文本本身不追加星号。 */
    BeatDisplay_FormatAmplitude(amplitude, config->amplitude_step, 0U);
    BeatDisplay_FormatPhase(phase, config->phase_step, 0U);

    /* 第一行绘制“信号1 / 信号2”列标题。 */
    BeatLcd_DrawChinesePair(102U, 6U, BEAT_CHINESE_XIN, BEAT_CHINESE_HAO,
                            LCD_TEXT_COLOR, LCD_BG_COLOR);
    BeatLcd_SetColors(LCD_TEXT_COLOR, LCD_BG_COLOR);
    BeatLcd_DrawText(168U, 14U, "1");
    BeatLcd_DrawChinesePair(220U, 6U, BEAT_CHINESE_XIN, BEAT_CHINESE_HAO,
                            LCD_TEXT_COLOR, LCD_BG_COLOR);
    BeatLcd_DrawText(286U, 14U, "2");

    /* 左侧依次绘制“波形、幅度、相位”行标题。 */
    BeatLcd_DrawChinesePair(8U, 60U, BEAT_CHINESE_BO, BEAT_CHINESE_XING,
                            LCD_TEXT_COLOR, LCD_BG_COLOR);
    BeatLcd_DrawChinesePair(8U, 120U, BEAT_CHINESE_FU, BEAT_CHINESE_DU,
                            LCD_TEXT_COLOR, LCD_BG_COLOR);
    BeatLcd_DrawChinesePair(8U, 180U, BEAT_CHINESE_XIANG, BEAT_CHINESE_WEI,
                            LCD_TEXT_COLOR, LCD_BG_COLOR);

    /* 编号 0~3 可编辑，编号 4 表示信号2固定显示项。 */
    BeatLcd_DrawWaveCell(0U, selected_item, 88U, 54U, config->wave1, editing);
    BeatLcd_DrawWaveCell(1U, selected_item, 206U, 54U, config->wave2, editing);
    BeatLcd_DrawMenuCell(2U, selected_item, 88U, 114U, 104U, amplitude, editing);
    BeatLcd_DrawMenuCell(4U, selected_item, 206U, 114U, 104U, "3.3V", 0U);
    BeatLcd_DrawMenuCell(3U, selected_item, 88U, 174U, 104U, phase, editing);
    BeatLcd_DrawMenuCell(4U, selected_item, 206U, 174U, 104U, "0DEG", 0U);
}

#elif BEAT_DISPLAY_BACKEND == BEAT_DISPLAY_OLED

/* OLED 是课程任务基线，也是当前默认编译后端。 */
#include "OLED_I2C.h"
#include "oled_canvas.h"

/** @brief 把一路样本映射到 OLED 指定纵向区域 */
static void BeatOled_DrawWave(const uint16_t *samples, uint16_t count,
                              uint16_t start_index, uint16_t scale,
                              uint8_t top, uint8_t bottom, const char *label)
{
    /* column 为 OLED 波形区横坐标；previous_* 用于连接上一采样点。 */
    uint8_t column;
    int16_t previous_x = 14;
    int16_t previous_y;
    /* value 保存当前源样本，sample_span 保存有效源区间长度。 */
    uint16_t value;
    uint16_t sample_span;

    /* 左侧 13 列放标签，右侧 114 列绘制波形。 */
    OledCanvas_DrawString(0U, (uint8_t)(top + 4U), label);
    /* 中值对应固定水平基线。 */
    OledCanvas_DrawLine(13, (int16_t)((top + bottom) / 2U), 127,
                        (int16_t)((top + bottom) / 2U));
    if ((samples == 0) || (count == 0U) || (start_index >= count) ||
        (scale == 0U))
    {
        return;
    }
    /* 将一帧样本压缩到 114 列，并用直线连接相邻采样点。 */
    sample_span = (uint16_t)(count - start_index);
    /* 第一个点单独计算，作为后续折线连接起点。 */
    value = samples[start_index] > scale ? scale : samples[start_index];
    previous_y = (int16_t)(bottom - (((uint32_t)value * (bottom - top) +
                                      scale / 2U) / scale));
    for (column = 1U; column < 114U; column++)
    {
        /* index 为当前 OLED 列对应的源样本下标，y 为映射后的纵坐标。 */
        uint16_t index = (uint16_t)(start_index +
                         ((uint32_t)column * (sample_span - 1U)) / 113U);
        int16_t y;

        /* 源样本按比例映射到当前横坐标，并钳位到通道量程。 */
        value = samples[index] > scale ? scale : samples[index];
        y = (int16_t)(bottom - (((uint32_t)value * (bottom - top) +
                                 scale / 2U) / scale));
        /* 相邻采样点使用直线连接。 */
        OledCanvas_DrawLine(previous_x, previous_y, (int16_t)(14 + column), y);
        previous_x = (int16_t)(14 + column);
        previous_y = y;
    }
}

/** @brief 在 OLED 画布上连续绘制两个 16×16 中文字 */
static void BeatOled_DrawChinesePair(uint8_t x, uint8_t y,
                                     BeatChineseGlyph first,
                                     BeatChineseGlyph second)
{
    OledCanvas_DrawChineseGlyph(x, y, first);
    OledCanvas_DrawChineseGlyph((uint8_t)(x + 16U), y, second);
}

/** @brief 使用四角短线标记 OLED 菜单当前选项 */
static void BeatOled_DrawFocusCell(uint8_t item, uint8_t selected_item,
                                   uint8_t x, uint8_t y)
{
    if (item == selected_item)
    {
        /* 选框比文字单元格略宽，预留编辑星号位置。 */
        int16_t left = (int16_t)x - 2;
        int16_t right = (int16_t)x + 43;
        int16_t top = y;
        int16_t bottom = (int16_t)y + 15;

        /* 四角短线代替整块反显，保证菜单文字在拍摄时仍为亮色。 */
        OledCanvas_DrawLine(left, top, (int16_t)(left + 3), top);
        OledCanvas_DrawLine(left, top, left, (int16_t)(top + 3));
        OledCanvas_DrawLine((int16_t)(right - 3), top, right, top);
        OledCanvas_DrawLine(right, top, right, (int16_t)(top + 3));
        OledCanvas_DrawLine(left, bottom, (int16_t)(left + 3), bottom);
        OledCanvas_DrawLine(left, (int16_t)(bottom - 3), left, bottom);
        OledCanvas_DrawLine((int16_t)(right - 3), bottom, right, bottom);
        OledCanvas_DrawLine(right, (int16_t)(bottom - 3), right, bottom);
    }
}

/** @brief 初始化 SSD1306 和 1024B 单色画布 */
uint8_t BeatDisplay_Init(void)
{
    /* 先初始化底层软件 I2C 和 OLED 控制器。 */
    oled_init();
    /* 上电清空并立即刷新，防止屏幕保留随机显存。 */
    OledCanvas_Clear();
    OledCanvas_Flush();
    return 1U;
}

/** @brief 显示 OLED 三路波形页 */
void BeatDisplay_ShowWave(const BeatConfig *config,
                          const BeatEngineStatus *status,
                          const BeatFrame *frame,
                          uint8_t frame_valid)
{
    /* delta_text 保存顶部频差字符串。 */
    char delta_text[9];
    /* OLED 当前只使用 frame_valid，status 保留用于统一接口。 */
    (void)status;
    BeatDisplay_FormatDelta(delta_text, config->delta_hz);
    /* OLED 每次先在 RAM 画布完成整帧，再按 page 一次性刷新。 */
    OledCanvas_Clear();
    /* 顶部显示频差；没有有效帧时右侧显示 WAIT ADC。 */
    OledCanvas_DrawString(0U, 0U, delta_text);
    if (frame_valid == 0U)
    {
        OledCanvas_DrawString(74U, 0U, "WAIT ADC");
    }
    /* X1/X2 使用 4095 量程，未缩放 X 使用 8190 量程。 */
    BeatOled_DrawWave(frame_valid != 0U ? frame->x1 : 0,
                      frame_valid != 0U ? frame->sample_count : 0U,
                      0U, 4095U, 10U, 26U, "X1");
    BeatOled_DrawWave(frame_valid != 0U ? frame->x2 : 0,
                      frame_valid != 0U ? frame->sample_count : 0U,
                      0U, 4095U, 29U, 45U, "X2");
    BeatOled_DrawWave(frame_valid != 0U ? frame->sum : 0,
                      frame_valid != 0U ? frame->sample_count : 0U,
                      0U, 8190U, 48U, 63U, "X");
    /* 所有图元完成后再整帧刷新，避免显示逐项绘制过程。 */
    OledCanvas_Flush();
}

/** @brief 显示 OLED 两列三行中文菜单 */
void BeatDisplay_ShowMenu(const BeatConfig *config,
                          uint8_t selected_item,
                          uint8_t editing)
{
    /* first/second 为波形中文名称，amplitude/phase 为参数文本。 */
    BeatChineseGlyph first;
    BeatChineseGlyph second;
    char amplitude[6];
    char phase[9];

    /* OLED 的编辑星号可直接附加在 ASCII 幅度和相位文本后。 */
    BeatDisplay_FormatAmplitude(amplitude, config->amplitude_step,
                                (uint8_t)((editing != 0U) && (selected_item == 2U)));
    BeatDisplay_FormatPhase(phase, config->phase_step,
                            (uint8_t)((editing != 0U) && (selected_item == 3U)));
    /* 每次菜单状态变化都从空画布完整重建。 */
    OledCanvas_Clear();
    /* 顶部绘制“信号1 / 信号2”。 */
    BeatOled_DrawChinesePair(34U, 0U, BEAT_CHINESE_XIN, BEAT_CHINESE_HAO);
    OledCanvas_DrawString(66U, 4U, "1");
    BeatOled_DrawChinesePair(84U, 0U, BEAT_CHINESE_XIN, BEAT_CHINESE_HAO);
    OledCanvas_DrawString(116U, 4U, "2");
    /* 左侧依次绘制“波形、幅度、相位”。 */
    BeatOled_DrawChinesePair(0U, 16U, BEAT_CHINESE_BO, BEAT_CHINESE_XING);
    BeatOled_DrawChinesePair(0U, 32U, BEAT_CHINESE_FU, BEAT_CHINESE_DU);
    BeatOled_DrawChinesePair(0U, 48U, BEAT_CHINESE_XIANG, BEAT_CHINESE_WEI);

    /* x1 波形单元格：名称、编辑星号和选中角标。 */
    BeatDisplay_GetWaveGlyphs(config->wave1, &first, &second);
    BeatOled_DrawChinesePair(34U, 16U, first, second);
    if ((selected_item == 0U) && (editing != 0U))
    {
        OledCanvas_DrawString(66U, 20U, "*");
    }
    BeatOled_DrawFocusCell(0U, selected_item, 34U, 16U);
    /* x2 波形单元格。 */
    BeatDisplay_GetWaveGlyphs(config->wave2, &first, &second);
    BeatOled_DrawChinesePair(84U, 16U, first, second);
    if ((selected_item == 1U) && (editing != 0U))
    {
        OledCanvas_DrawString(116U, 20U, "*");
    }
    BeatOled_DrawFocusCell(1U, selected_item, 84U, 16U);
    /* x1 幅度和相位可编辑，x2 的幅度和相位只读。 */
    OledCanvas_DrawString(34U, 36U, amplitude);
    BeatOled_DrawFocusCell(2U, selected_item, 34U, 32U);
    OledCanvas_DrawString(84U, 36U, "3.3V");
    OledCanvas_DrawString(34U, 52U, phase);
    BeatOled_DrawFocusCell(3U, selected_item, 34U, 48U);
    OledCanvas_DrawString(84U, 52U, "0DEG");
    /* 菜单完整绘制后统一刷新。 */
    OledCanvas_Flush();
}

/* OLED 保持课程作业原交互，示波器控制接口在该后端不占用按键。 */
void BeatDisplay_ScopeNextControl(void)
{
}

uint8_t BeatDisplay_ScopeAdjust(int16_t steps)
{
    (void)steps;
    return 0U;
}

void BeatDisplay_ScopeToggleRun(void)
{
}

uint8_t BeatDisplay_ScopeIsRunning(void)
{
    return 1U;
}

#else
#error Unsupported BEAT_DISPLAY_BACKEND
#endif
