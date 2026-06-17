#ifndef __SSD1306_H
#define __SSD1306_H

#include <stdint.h>
#include "Bsp/oled_i2c.h"

#define SSD1306_WIDTH     128
#define SSD1306_HEIGHT    64
#define SSD1306_I2C_ADDR  0x78
#define SSD1306_CHAR_W    6
#define SSD1306_TEXT_COLS (SSD1306_WIDTH / SSD1306_CHAR_W)

uint8_t SSD1306_Init(void);
void SSD1306_SetContrast(uint8_t val);
void SSD1306_Clear(void);
void SSD1306_WriteLine(uint8_t page, const char *text);

#endif
