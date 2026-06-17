#ifndef __SYSTEM_H
#define __SYSTEM_H

#include <stdint.h>
#include "App/app.h"

void System_Init(void);
void System_Run(void);
uint32_t System_GetTick(void);
void System_UartWrite(const uint8_t *data, uint8_t len);

#endif
