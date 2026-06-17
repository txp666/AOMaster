#include "Bsp/oled_i2c.h"
#include "Bsp/i2c_bus.h"
#include "Sys/system.h"
#include "Sys/log.h"

#define I2C_SYNC_TIMEOUT_MS     50U
#define OLED_I2C_TX_MAX         140U

static uint8_t s_sync_ok;

static struct
{
    uint8_t active;
    uint8_t addr;
    uint8_t buf[OLED_I2C_TX_MAX];
    uint8_t len;
} s_legacy;

static void OLED_I2C_SyncCb(I2C_BusStatus_t status, void *ctx)
{
    (void)ctx;
    s_sync_ok = (status == I2C_BUS_OK) ? 0 : 1;
}

static uint8_t OLED_I2C_WaitIdle(uint32_t start)
{
    while(!I2C_Bus_IsIdle())
    {
        I2C_Bus_Poll();
        if((System_GetTick() - start) > I2C_SYNC_TIMEOUT_MS)
            return 1;
    }
    return 0;
}

static uint8_t OLED_I2C_RunSync(uint8_t addr, const uint8_t *data, uint8_t len, uint8_t probe)
{
    uint32_t start;

    start = System_GetTick();
    if(OLED_I2C_WaitIdle(start))
    {
        LOGE("oled i2c wait idle timeout\r\n");
        return 1;
    }

    s_sync_ok = 1;
    if(probe)
    {
        if(I2C_Bus_SubmitProbe(addr, OLED_I2C_SyncCb, 0))
            return 1;
    }
    else
    {
        if(I2C_Bus_SubmitWrite(addr, data, len, OLED_I2C_SyncCb, 0))
            return 1;
    }

    start = System_GetTick();
    if(OLED_I2C_WaitIdle(start))
    {
        LOGE("oled i2c sync timeout\r\n");
        I2C_Bus_Abort();
        return 1;
    }

    return s_sync_ok;
}

void OLED_I2C_Recover(void)
{
    I2C_Bus_Abort();
    I2C_Bus_Init();
}

void OLED_I2C_Init(void)
{
    I2C_Bus_Init();
    s_legacy.active = 0;
    s_legacy.len = 0;
}

uint8_t OLED_I2C_Probe(uint8_t addr)
{
    return OLED_I2C_RunSync(addr, 0, 0, 1);
}

uint8_t OLED_I2C_Start(uint8_t addr)
{
    if(s_legacy.active)
        return 1;

    s_legacy.addr = addr;
    s_legacy.len = 0;
    s_legacy.active = 1;
    return 0;
}

uint8_t OLED_I2C_Write(uint8_t data)
{
    if(!s_legacy.active)
        return 1;
    if(s_legacy.len >= OLED_I2C_TX_MAX)
        return 1;

    s_legacy.buf[s_legacy.len++] = data;
    return 0;
}

void OLED_I2C_Stop(void)
{
    if(!s_legacy.active)
        return;

    if(s_legacy.len > 0)
        OLED_I2C_RunSync(s_legacy.addr, s_legacy.buf, s_legacy.len, 0);

    s_legacy.active = 0;
    s_legacy.len = 0;
}
