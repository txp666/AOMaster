#ifndef __POWER_H
#define __POWER_H

#include <stdint.h>

#define POWER_IDLE_TIMEOUT_MS   60000U

void Power_Init(void);
void Power_Touch(void);
void Power_Idle(void);

#endif
