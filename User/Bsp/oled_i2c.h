#ifndef __OLED_I2C_H
#define __OLED_I2C_H

#include "ch32v00x.h"

#define OLED_I2C_CLKRATE 400000
#define OLED_I2C_SDA_PIN GPIO_Pin_1
#define OLED_I2C_SCL_PIN GPIO_Pin_2

void OLED_I2C_Init(void);
void OLED_I2C_Recover(void);
uint8_t OLED_I2C_Probe(uint8_t addr);
uint8_t OLED_I2C_Start(uint8_t addr);
uint8_t OLED_I2C_Write(uint8_t data);
void OLED_I2C_Stop(void);

#endif
