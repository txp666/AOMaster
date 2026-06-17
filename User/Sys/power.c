#include "Sys/power.h"
#include "Sys/system.h"
#include "App/settings.h"
#include "debug.h"

static uint32_t s_last_activity_ms;
static uint8_t s_auto_sleep;

void Power_Init(void)
{
    s_last_activity_ms = 0;
    s_auto_sleep = 1;
}

void Power_Touch(void)
{
    s_last_activity_ms = System_GetTick();
}

void Power_Idle(void)
{
    if(!s_auto_sleep)
        return;

    Settings_FlushPending();

    /* Enter WFI after work; EXTI, TIM2, or TIM1 can wake the core. */
    __WFI();
}
