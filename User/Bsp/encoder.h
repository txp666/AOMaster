#ifndef __ENCODER_H
#define __ENCODER_H

#include <stdint.h>

/* EC11: PD4=A, PD2=B, PD3=KEY(active low) */
#define ENC_GPIO_A      GPIOD
#define ENC_PIN_A       GPIO_Pin_4
#define ENC_GPIO_B      GPIOD
#define ENC_PIN_B       GPIO_Pin_2
#define ENC_GPIO_PUSH   GPIOD
#define ENC_PIN_PUSH    GPIO_Pin_3

void Encoder_Init(void);
void Encoder_Update(void);
int16_t Encoder_GetDiff(void);
uint8_t Encoder_IsPressed(void);
uint8_t Encoder_GetClick(void);
uint8_t Encoder_GetLongClick(void);
void Encoder_SetEnable(uint8_t en);
void Encoder_IRQHandler(void);

#endif
