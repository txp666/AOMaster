#ifndef __APP_H
#define __APP_H

#include <stdint.h>

typedef enum
{
    APP_ST_BOOT_DELAY = 0,
    APP_ST_OLED_INIT,
    APP_ST_OLED_DRAW,
    APP_ST_GP8630_START,
    APP_ST_GP8630_WAIT,
    APP_ST_RUN,
} AppState_t;

void App_Begin(void);
void App_Update(void);
void App_OnSerialByte(uint8_t byte);
void App_SerialPoll(void);
AppState_t App_GetState(void);
const char *App_GetStateStr(void);

#endif
