#ifndef __I2C_BUS_H
#define __I2C_BUS_H

#include <stdint.h>

typedef enum
{
    I2C_BUS_IDLE = 0,
    I2C_BUS_BUSY,
    I2C_BUS_OK,
    I2C_BUS_ERR,
} I2C_BusStatus_t;

typedef void (*I2C_BusDoneCb)(I2C_BusStatus_t status, void *ctx);

void I2C_Bus_Init(void);
void I2C_Bus_Poll(void);
I2C_BusStatus_t I2C_Bus_GetStatus(void);
uint8_t I2C_Bus_IsIdle(void);
uint32_t I2C_Bus_GetErrCount(void);

uint8_t I2C_Bus_SubmitProbe(uint8_t addr8, I2C_BusDoneCb cb, void *ctx);
uint8_t I2C_Bus_SubmitWrite(uint8_t addr8, const uint8_t *data, uint8_t len,
                            I2C_BusDoneCb cb, void *ctx);
void I2C_Bus_Abort(void);

#endif
