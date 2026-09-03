/**
  ******************************************************************************
  * @file    oled_canvas.c
  * @brief   OLED 单色帧缓冲、基础图元和 page 批量刷新
  ******************************************************************************
  */

#include "oled_canvas.h"

#include <string.h>

#include "OLED_I2C.h"

/*
 * s_canvas 和字库都声明为 static，只服务于本画布模块。
 * 字库再加 const，表示运行中不可修改，可由链接器放入 Flash。
 */

/* SSD1306 64 行分为 8 个 page，每个 page 包含 128 列。 */
static uint8_t s_canvas[8][128];

/** @brief 软件 I2C 短延时，满足 SCL/SDA 建立和保持时间 */
static void OledCanvas_I2cDelay(void)
{
    uint8_t i = 10;
    while (i-- != 0)
    {
        __NOP();
    }
}

/** @brief 产生软件 I2C START 条件 */
static void OledCanvas_I2cStart(void)
{
    /* 总线空闲时 SCL、SDA 均为高电平。 */
    OLED_IIC_SDA_GPIO->BSRR = OLED_IIC_SDA_PIN;
    OLED_IIC_SCL_GPIO->BSRR = OLED_IIC_SCL_PIN;
    OledCanvas_I2cDelay();
    /* SCL 为高时 SDA 从高拉低，形成 START。 */
    OLED_IIC_SDA_GPIO->BRR = OLED_IIC_SDA_PIN;
    OledCanvas_I2cDelay();
    OLED_IIC_SCL_GPIO->BRR = OLED_IIC_SCL_PIN;
}

/** @brief 产生软件 I2C STOP 条件 */
static void OledCanvas_I2cStop(void)
{
    /* 先保证 SDA 为低，再释放 SCL。 */
    OLED_IIC_SCL_GPIO->BRR = OLED_IIC_SCL_PIN;
    OLED_IIC_SDA_GPIO->BRR = OLED_IIC_SDA_PIN;
    OledCanvas_I2cDelay();
    OLED_IIC_SCL_GPIO->BSRR = OLED_IIC_SCL_PIN;
    OledCanvas_I2cDelay();
    /* SCL 为高时 SDA 从低释放到高，形成 STOP。 */
    OLED_IIC_SDA_GPIO->BSRR = OLED_IIC_SDA_PIN;
    OledCanvas_I2cDelay();
}

/**
 * @brief  软件 I2C 发送一个字节，最高位先发送
 * @note   OLED 连续写场景不读取 ACK，只提供第 9 个时钟。
 */
static void OledCanvas_I2cWriteByte(uint8_t data)
{
    /* i 遍历一个字节的 8 个数据位。 */
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        /* 根据当前最高位设置 SDA。 */
        if ((data & 0x80) != 0)
        {
            OLED_IIC_SDA_GPIO->BSRR = OLED_IIC_SDA_PIN;
        }
        else
        {
            OLED_IIC_SDA_GPIO->BRR = OLED_IIC_SDA_PIN;
        }
        OledCanvas_I2cDelay();
        /* SCL 拉高后，从机采样当前 SDA。 */
        OLED_IIC_SCL_GPIO->BSRR = OLED_IIC_SCL_PIN;
        OledCanvas_I2cDelay();
        OLED_IIC_SCL_GPIO->BRR = OLED_IIC_SCL_PIN;
        /* 下一位移动到最高位。 */
        data <<= 1;
    }

    /* 第 9 个时钟释放 SDA，给从机 ACK 留出时隙。 */
    OLED_IIC_SDA_GPIO->BSRR = OLED_IIC_SDA_PIN;
    OledCanvas_I2cDelay();
    OLED_IIC_SCL_GPIO->BSRR = OLED_IIC_SCL_PIN;
    OledCanvas_I2cDelay();
    OLED_IIC_SCL_GPIO->BRR = OLED_IIC_SCL_PIN;
}

/** @brief 连续发送当前 SSD1306 page 的 128 字节显示数据 */
static void OledCanvas_WritePage(const uint8_t *data)
{
    /* column 遍历 SSD1306 一页的 128 列。 */
    uint8_t column;

    OledCanvas_I2cStart();
    /* 先发送设备写地址，再发送 0x40 表示后续为显示数据。 */
    OledCanvas_I2cWriteByte(OLED_ADDRESS);
    OledCanvas_I2cWriteByte(0x40);
    /* 一次连续发送整页 128 字节，减少逐像素 I2C 开销。 */
    for (column = 0; column < 128; column++)
    {
        OledCanvas_I2cWriteByte(data[column]);
    }
    OledCanvas_I2cStop();
}

/* 10 个数字，每个字符由 5 列位图组成。 */
static const uint8_t s_digit_font[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}
};

/* 26 个大写英文字母，每个字符同样使用 5 列位图。 */
static const uint8_t s_letter_font[26][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x51, 0x49, 0x45, 0x43}
};

/** @brief 从内置 5×7 字库取得一个 ASCII 字形 */
static void OledCanvas_GetGlyph(char character, uint8_t glyph[5])
{
    /* i 用于清空或复制字形的 5 列数据。 */
    uint8_t i;

    /* 默认清空，未支持字符会显示为空格。 */
    for (i = 0; i < 5; i++)
    {
        glyph[i] = 0;
    }

    /* 数字和大写字母直接从静态表复制。 */
    if ((character >= '0') && (character <= '9'))
    {
        for (i = 0; i < 5; i++)
        {
            glyph[i] = s_digit_font[character - '0'][i];
        }
    }
    else if ((character >= 'A') && (character <= 'Z'))
    {
        for (i = 0; i < 5; i++)
        {
            glyph[i] = s_letter_font[character - 'A'][i];
        }
    }
    /* 页面实际使用的少量符号单独定义。 */
    else if (character == ':')
    {
        glyph[2] = 0x36;
    }
    else if (character == '.')
    {
        glyph[2] = 0x40;
    }
    else if (character == '-')
    {
        glyph[1] = 0x08;
        glyph[2] = 0x08;
        glyph[3] = 0x08;
    }
    else if (character == '+')
    {
        glyph[2] = 0x1C;
        glyph[1] = 0x08;
        glyph[3] = 0x08;
    }
    else if (character == '*')
    {
        glyph[0] = 0x14;
        glyph[1] = 0x08;
        glyph[2] = 0x3E;
        glyph[3] = 0x08;
        glyph[4] = 0x14;
    }
    else if (character == '=')
    {
        glyph[1] = 0x14;
        glyph[2] = 0x14;
        glyph[3] = 0x14;
    }
}

/** @brief 把整张 RAM 画布清零 */
void OledCanvas_Clear(void)
{
    memset(s_canvas, 0, sizeof(s_canvas));
}

/** @brief 设置或清除一个画布像素 */
void OledCanvas_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    /* mask 只选中当前 page 字节中与 y 对应的一个位。 */
    uint8_t mask;

    /* 所有上层图元都通过这里统一裁剪越界坐标。 */
    if ((x >= 128) || (y >= 64))
    {
        return;
    }

    /* y/8 选择 page，y%8 选择该字节中的具体位。 */
    mask = (uint8_t)(1U << (y & 0x07));
    if (on != 0)
    {
        s_canvas[y >> 3][x] |= mask;
    }
    else
    {
        s_canvas[y >> 3][x] &= (uint8_t)~mask;
    }
}

/** @brief 使用 Bresenham 整数算法绘制任意方向直线 */
void OledCanvas_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    /* dx/dy 表示距离，sx/sy 表示两个方向每步的符号。 */
    int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t error = dx + dy;

    /* Bresenham 整数算法，不使用浮点运算。 */
    while (1)
    {
        /* 当前点位于屏幕内时才写入画布。 */
        if ((x0 >= 0) && (x0 < 128) && (y0 >= 0) && (y0 < 64))
        {
            OledCanvas_SetPixel((uint8_t)x0, (uint8_t)y0, 1);
        }

        /* 到达终点后结束。 */
        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }

        /* 根据误差项决定本轮是否推进 x 和 y。 */
        if ((error << 1) >= dy)
        {
            error += dy;
            x0 += sx;
        }
        if ((error << 1) <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}

/** @brief 使用四条直线绘制空心矩形 */
void OledCanvas_DrawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    /* 零尺寸图形不执行绘制。 */
    if ((width == 0U) || (height == 0U))
    {
        return;
    }

    OledCanvas_DrawLine(x, y, (int16_t)(x + width - 1U), y);
    OledCanvas_DrawLine(x, (int16_t)(y + height - 1U),
                        (int16_t)(x + width - 1U), (int16_t)(y + height - 1U));
    OledCanvas_DrawLine(x, y, x, (int16_t)(y + height - 1U));
    OledCanvas_DrawLine((int16_t)(x + width - 1U), y,
                        (int16_t)(x + width - 1U), (int16_t)(y + height - 1U));
}

/** @brief 逐像素填充或清除矩形区域 */
void OledCanvas_FillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t on)
{
    /* 使用 16 位循环变量，避免 x+width、y+height 的 8 位溢出。 */
    uint16_t row;
    uint16_t col;

    /* 循环条件同时处理目标边界和屏幕边界。 */
    for (row = y; (row < (uint16_t)y + height) && (row < 64U); row++)
    {
        for (col = x; (col < (uint16_t)x + width) && (col < 128U); col++)
        {
            OledCanvas_SetPixel((uint8_t)col, (uint8_t)row, on);
        }
    }
}

/** @brief 使用 5×7 字形和 1 列间距绘制 ASCII 字符串 */
void OledCanvas_DrawString(uint8_t x, uint8_t y, const char *text)
{
    /* glyph 保存当前字符的 5 列位图；col/row 遍历像素。 */
    uint8_t glyph[5];
    uint8_t col;
    uint8_t row;

    if (text == 0)
    {
        return;
    }

    /* 每个字符占 6 列，x>122 后不足以完整显示一个字符。 */
    while ((*text != '\0') && (x <= 122))
    {
        /* 取得 5 列字形，再逐列逐位写入点亮像素。 */
        OledCanvas_GetGlyph(*text, glyph);
        for (col = 0; col < 5; col++)
        {
            for (row = 0; row < 7; row++)
            {
                if ((glyph[col] & (1U << row)) != 0)
                {
                    OledCanvas_SetPixel((uint8_t)(x + col), (uint8_t)(y + row), 1);
                }
            }
        }
        /* 5 列字形后留 1 列空白。 */
        x = (uint8_t)(x + 6);
        text++;
    }
}

/** @brief 绘制一个 16×16 项目专用中文字模 */
void OledCanvas_DrawChineseGlyph(uint8_t x, uint8_t y, BeatChineseGlyph glyph)
{
    /* row/col 遍历 16×16 中文点阵。 */
    uint8_t row;
    uint8_t col;

    /* 非法枚举不访问字库。 */
    if (glyph >= BEAT_CHINESE_COUNT)
    {
        return;
    }

    /* 字库每行使用 16 位，高位对应左侧像素。 */
    for (row = 0U; row < 16U; row++)
    {
        for (col = 0U; col < 16U; col++)
        {
            if ((BeatChineseFont_GetRow(glyph, row) &
                 (uint16_t)(1U << (15U - col))) != 0U)
            {
                OledCanvas_SetPixel((uint8_t)(x + col), (uint8_t)(y + row), 1U);
            }
        }
    }
}

/** @brief 对矩形区域中的每个像素执行 XOR 反显 */
void OledCanvas_InvertRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    /* row/col 遍历要执行 XOR 的目标区域。 */
    uint8_t row;
    uint8_t col;

    for (row = y; (row < (uint8_t)(y + height)) && (row < 64); row++)
    {
        for (col = x; (col < (uint8_t)(x + width)) && (col < 128); col++)
        {
            /* XOR 只翻转目标位，不影响同一字节中的其他 7 个像素。 */
            s_canvas[row >> 3][col] ^= (uint8_t)(1U << (row & 0x07));
        }
    }
}

/** @brief 把 1024B 画布完整刷新到 SSD1306 */
void OledCanvas_Flush(void)
{
    /* page 依次选择 SSD1306 的 0~7 页。 */
    uint8_t page;

    /* SSD1306 共 8 个 page，每页覆盖 128 列（包含物理第 0 列）。 */
    for (page = 0; page < 8; page++)
    {
        /* 每页都从物理第 0 列开始，随后连续写入完整 128 列。 */
        oled_set_pos(0, page);
        OledCanvas_WritePage(&s_canvas[page][0]);
    }
}
