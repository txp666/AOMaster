#ifndef __BUZZ_H
#define __BUZZ_H

#include <stdint.h>

#define BUZZ_GPIO       GPIOC
#define BUZZ_PIN        GPIO_Pin_0

void Buzz_Init(void);
void Buzz_SetEnable(uint8_t en);
void Buzz_Tone(uint32_t freq, uint32_t duration_ms);
void Buzz_Update(uint32_t elapsed_ms);

#endif
