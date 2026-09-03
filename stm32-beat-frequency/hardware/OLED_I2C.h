#ifndef __OLED_I2C_H
#define	__OLED_I2C_H

#include "stm32f10x.h"

#define OLED_ADDRESS	0x78 //通过调整0R电阻,屏可以0x78和0x7A两个地址 -- 默认0x78

/* Software I2C pins (SCL=PC10, SDA=PB15) */
#define OLED_IIC_SCL_GPIO GPIOC
#define OLED_IIC_SCL_PIN  GPIO_Pin_10
#define OLED_IIC_SCL_RCC  RCC_APB2Periph_GPIOC

#define OLED_IIC_SDA_GPIO GPIOB
#define OLED_IIC_SDA_PIN  GPIO_Pin_15
#define OLED_IIC_SDA_RCC  RCC_APB2Periph_GPIOB

#define OLED_IIC_DELAY_US 5

/* New-style API (you can rename later) */
void oled_i2c_init(void);
void oled_write_cmd(uint8_t cmd);
void oled_write_data(uint8_t data);
void oled_init(void);
void oled_set_pos(uint8_t x, uint8_t y);
void oled_fill(uint8_t data);
void oled_clear(void);
void oled_on(void);
void oled_off(void);
void oled_show_str(uint8_t x, uint8_t y, uint8_t *ch, uint8_t text_size);
void oled_show_cn(uint8_t x, uint8_t y, uint8_t index);
void oled_draw_bmp(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t *bmp);

#endif
