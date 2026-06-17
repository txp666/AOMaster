#ifndef __GP8630_H
#define __GP8630_H

#include <stdint.h>

/* I2C bus layer writes addr directly to DATAR, so use 8-bit write addresses.
 * GP8630 7-bit base 0x58 (A0/A1/A2 -> 0x58..0x5F) => 8-bit 0xB0..0xBE step 2. */
#define GP8630_I2C_ADDR_DEFAULT 0xB0U
#define GP8630_I2C_ADDR_LAST    0xBEU
#define GP8630_I2C_ADDR_STEP    0x02U
#define GP8630_REG_MODE         0x01U
#define GP8630_REG_DATA         0x02U
#define GP8630_CODE_MAX         0xFFFFU

typedef enum
{
    GP8630_OUT_VOLT_10V = 0,
    GP8630_OUT_VOLT_12V,
    GP8630_OUT_CURR_20MA,
    GP8630_OUT_CURR_24MA,
} GP8630_OutputMode_t;

typedef enum
{
    GP8630_INIT_IDLE = 0,
    GP8630_INIT_BUSY,
    GP8630_INIT_OK,
    GP8630_INIT_FAIL,
} GP8630_InitState_t;

void GP8630_BeginInit(void);
void GP8630_BeginInitWithMode(GP8630_OutputMode_t mode);
void GP8630_Update(void);
GP8630_InitState_t GP8630_GetInitState(void);
const char *GP8630_GetInitStateStr(void);
uint8_t GP8630_GetI2cAddr(void);
uint8_t GP8630_IsReady(void);

uint8_t GP8630_SetMode(GP8630_OutputMode_t mode);
GP8630_OutputMode_t GP8630_GetMode(void);
void GP8630_RequestPercent(uint16_t permille);
void GP8630_RequestCode(uint16_t code);
uint8_t GP8630_SetVoltage_mV(uint32_t mv);
uint8_t GP8630_SetCurrent_uA(uint32_t ua);

#endif
