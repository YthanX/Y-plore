/************************************************************************************
*  Copyright (c), 2014, HelTec Automatic Technology co.,LTD.
*            All rights reserved.
*
* Http:    www.heltec.cn
* Email:   cn.heltec@gmail.com
* WebShop: heltec.taobao.com
*
* File name: OLED_I2C.c
* Project  : project-ministm32f103rct6-LTS
* Processor: STM32F103RCT6
* Compiler : MDK for ARM
* 
* Author : 小林
* Version: 1.00
* Date   : 2014.4.8
* Email  : hello14blog@gmail.com
* Modification: none
* 
* Description: 128x64 OLED(SSD1306)驱动，基于GPIO模拟I2C，
*              适配本工程引脚：SCL=PC10，SDA=PB15。
*
* Others: none;
*
* Function List:
* 1. void oled_i2c_init(void) -- 初始化GPIO模拟I2C
* 2. void oled_write_cmd(uint8_t cmd) -- 写命令
* 3. void oled_write_data(uint8_t data) -- 写数据
* 4. void oled_init(void) -- OLED初始化
* 5. void oled_set_pos(uint8_t x, uint8_t y) -- 设置起始坐标
* 6. void oled_fill(uint8_t data) -- 全屏填充
* 7. void oled_clear(void) -- 清屏
* 8. void oled_on(void) -- 唤醒
* 9. void oled_off(void) -- 睡眠
* 10. void oled_show_str(uint8_t x, uint8_t y, uint8_t *ch, uint8_t text_size) -- 显示字符串(6x8/8x16)
* 11. void oled_show_cn(uint8_t x, uint8_t y, uint8_t index) -- 显示中文(字模见 codetab.h)
* 12. void oled_draw_bmp(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t *bmp) -- 显示BMP
*
* History: none;
*
*************************************************************************************/

#include "OLED_I2C.h"
#include "Delay.h"
#include "codetab.h"

static void oled_i2c_delay(void)
{
	// 72MHz下，一个NOP大约14ns。开漏输出受限于外部上拉电阻，上升沿比较慢。
	// 为了确保时序满足，并给高电平足够的上升时间，我们使用适量的NOP延迟（约1us内）
	uint8_t i = 10;
	while(i--) {
		__NOP();
	}
}

// 注意：这里删除了原来耗时的 oled_sda_out 和 oled_sda_in 函数
// 因为开漏输出（Open-Drain）模式下，只要输出高电平（1），引脚就处于高阻态，
// 此时可以直接读取外部电平状态，无需切换回输入模式！

static void oled_scl_high(void)
{
	OLED_IIC_SCL_GPIO->BSRR = OLED_IIC_SCL_PIN;
}

static void oled_scl_low(void)
{
	OLED_IIC_SCL_GPIO->BRR = OLED_IIC_SCL_PIN;
}

static void oled_sda_high(void)
{
	OLED_IIC_SDA_GPIO->BSRR = OLED_IIC_SDA_PIN;
}

static void oled_sda_low(void)
{
	OLED_IIC_SDA_GPIO->BRR = OLED_IIC_SDA_PIN;
}

static uint8_t oled_sda_read(void)
{
	// 直接读取输入寄存器 (IDR)
	if ((OLED_IIC_SDA_GPIO->IDR & OLED_IIC_SDA_PIN) != 0) {
		return 1;
	} else {
		return 0;
	}
}

static void oled_i2c_start(void)
{
	oled_sda_high();
	oled_scl_high();
	oled_i2c_delay();
	oled_sda_low();
	oled_i2c_delay();
	oled_scl_low();
}

static void oled_i2c_stop(void)
{
	oled_scl_low();
	oled_sda_low();
	oled_i2c_delay();
	oled_scl_high();
	oled_i2c_delay();
	oled_sda_high();
	oled_i2c_delay();
}

static uint8_t oled_i2c_write_byte(uint8_t data)
{
	uint8_t i;
	uint8_t ack;

	for (i = 0; i < 8; i++)
	{
		if (data & 0x80)
		{
			oled_sda_high();
		}
		else
		{
			oled_sda_low();
		}
		oled_i2c_delay();
		oled_scl_high();
		oled_i2c_delay();
		oled_scl_low();
		data <<= 1;
	}

	// 等待ACK：将SDA拉高（释放总线），从机如果应答会拉低SDA
	oled_sda_high(); 
	oled_i2c_delay();
	oled_scl_high();
	oled_i2c_delay();
	
	ack = (oled_sda_read() == 0) ? 1 : 0;
	
	oled_scl_low();

	return ack;
}

void oled_i2c_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(OLED_IIC_SCL_RCC | OLED_IIC_SDA_RCC, ENABLE);

	// SCL 和 SDA 均配置为开漏输出
	GPIO_InitStructure.GPIO_Pin = OLED_IIC_SCL_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_Init(OLED_IIC_SCL_GPIO, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = OLED_IIC_SDA_PIN;
	GPIO_Init(OLED_IIC_SDA_GPIO, &GPIO_InitStructure);

	oled_scl_high();
	oled_sda_high();
}

void oled_write_cmd(uint8_t cmd)
{
	oled_i2c_start();
	(void)oled_i2c_write_byte(OLED_ADDRESS);
	(void)oled_i2c_write_byte(0x00);
	(void)oled_i2c_write_byte(cmd);
	oled_i2c_stop();
}

void oled_write_data(uint8_t data)
{
	oled_i2c_start();
	(void)oled_i2c_write_byte(OLED_ADDRESS);
	(void)oled_i2c_write_byte(0x40);
	(void)oled_i2c_write_byte(data);
	oled_i2c_stop();
}

void oled_init(void)
{
	oled_i2c_init();
	Delay_ms(100);

	oled_write_cmd(0xAE);
	oled_write_cmd(0x20);
	oled_write_cmd(0x10);
	oled_write_cmd(0xb0);
	oled_write_cmd(0xc8);
	oled_write_cmd(0x00);
	oled_write_cmd(0x10);
	oled_write_cmd(0x40);
	oled_write_cmd(0x81);
	oled_write_cmd(0xff);
	oled_write_cmd(0xa1);
	oled_write_cmd(0xa6);
	oled_write_cmd(0xa8);
	oled_write_cmd(0x3F);
	oled_write_cmd(0xa4);
	oled_write_cmd(0xd3);
	oled_write_cmd(0x00);
	oled_write_cmd(0xd5);
	oled_write_cmd(0xf0);
	oled_write_cmd(0xd9);
	oled_write_cmd(0x22);
	oled_write_cmd(0xda);
	oled_write_cmd(0x12);
	oled_write_cmd(0xdb);
	oled_write_cmd(0x20);
	oled_write_cmd(0x8d);
	oled_write_cmd(0x14);
	oled_write_cmd(0xaf);
}

void oled_set_pos(uint8_t x, uint8_t y)
{
	oled_write_cmd(0xb0 + y);
	oled_write_cmd(((x & 0xf0) >> 4) | 0x10);
	oled_write_cmd(x & 0x0f);
}

void oled_fill(uint8_t data)
{
	uint8_t m;
	uint8_t n;

	for (m = 0; m < 8; m++)
	{
		oled_write_cmd(0xb0 + m);
		oled_write_cmd(0x00);
		oled_write_cmd(0x10);
		for (n = 0; n < 128; n++)
		{
			oled_write_data(data);
		}
	}
}

void oled_clear(void)
{
	oled_fill(0x00);
}

void oled_on(void)
{
	oled_write_cmd(0x8D);
	oled_write_cmd(0x14);
	oled_write_cmd(0xAF);
}

void oled_off(void)
{
	oled_write_cmd(0x8D);
	oled_write_cmd(0x10);
	oled_write_cmd(0xAE);
}

void oled_show_str(uint8_t x, uint8_t y, uint8_t *ch, uint8_t text_size)
{
	uint8_t c = 0;
	uint8_t i = 0;
	uint8_t j = 0;

	switch (text_size)
	{
		case 1:
		{
			while (ch[j] != '\0')
			{
				c = ch[j] - 32;
				if (x > 126)
				{
					x = 0;
					y++;
				}
				oled_set_pos(x, y);
				for (i = 0; i < 6; i++)
				{
					oled_write_data(F6x8[c][i]);
				}
				x += 6;
				j++;
			}
		}
		break;
		case 2:
		{
			while (ch[j] != '\0')
			{
				c = ch[j] - 32;
				if (x > 120)
				{
					x = 0;
					y++;
				}
				oled_set_pos(x, y);
				for (i = 0; i < 8; i++)
				{
					oled_write_data(F8X16[c * 16 + i]);
				}
				oled_set_pos(x, y + 1);
				for (i = 0; i < 8; i++)
				{
					oled_write_data(F8X16[c * 16 + i + 8]);
				}
				x += 8;
				j++;
			}
		}
		break;
		default:
			break;
	}
}

void oled_show_cn(uint8_t x, uint8_t y, uint8_t index)
{
	uint8_t wm = 0;
	uint16_t adder = 32 * index;

	oled_set_pos(x, y);
	for (wm = 0; wm < 16; wm++)
	{
		oled_write_data(F16x16[adder]);
		adder += 1;
	}
	oled_set_pos(x, y + 1);
	for (wm = 0; wm < 16; wm++)
	{
		oled_write_data(F16x16[adder]);
		adder += 1;
	}
}

void oled_draw_bmp(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t *bmp)
{
	uint16_t j = 0;
	uint8_t x;
	uint8_t y;

	if (y1 % 8 == 0)
	{
		y = y1 / 8;
	}
	else
	{
		y = y1 / 8 + 1;
	}
	for (y = y0; y < y1; y++)
	{
		oled_set_pos(x0, y);
		for (x = x0; x < x1; x++)
		{
			oled_write_data(bmp[j++]);
		}
	}
}
