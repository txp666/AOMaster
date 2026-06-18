#include "Drv/gp8630.h"
#include "Bsp/i2c_bus.h"
#include "Sys/log.h"

#define GP8630_MODE_VOLT_10V    0x1CU
#define GP8630_MODE_VOLT_12V    0x18U
#define GP8630_MODE_CURR_20MA   0x24U
#define GP8630_MODE_CURR_24MA   0x20U
#define GP8630_INIT_RETRY_MAX   3U

typedef enum
{
    G_SM_IDLE = 0,
    G_SM_PROBE,
    G_SM_SET_MODE,
    G_SM_ZERO,
    G_SM_READY,
    G_SM_FAIL,
} GP8630_SmState_t;

typedef enum
{
    G_TX_NONE = 0,
    G_TX_PROBE,
    G_TX_MODE,
    G_TX_DATA,
} GP8630_TxKind_t;

static GP8630_OutputMode_t s_mode = GP8630_OUT_VOLT_10V;
static GP8630_SmState_t s_sm = G_SM_IDLE;
static GP8630_InitState_t s_init_st = GP8630_INIT_IDLE;
static GP8630_TxKind_t s_tx_kind = G_TX_NONE;
static uint8_t s_ready;
static uint8_t s_pending;
static uint8_t s_i2c_addr;
static uint8_t s_probe_addr;
static uint8_t s_init_retry;
static uint16_t s_pending_code;

static void GP8630_RestartProbe(void)
{
    s_probe_addr = GP8630_I2C_ADDR_DEFAULT;
    s_sm = G_SM_PROBE;
    s_init_st = GP8630_INIT_BUSY;
    s_ready = 0;
    s_tx_kind = G_TX_NONE;
}

static void GP8630_OnTxDone(I2C_BusStatus_t status, void *ctx)
{
    (void)ctx;

    if(status != I2C_BUS_OK)
    {
        if(s_tx_kind == G_TX_PROBE)
        {
            if(s_probe_addr < GP8630_I2C_ADDR_LAST)
            {
                LOGW("gp8630 no ack 0x%02X\r\n", (unsigned)s_probe_addr);
                s_probe_addr += GP8630_I2C_ADDR_STEP;
                s_sm = G_SM_PROBE;
                s_tx_kind = G_TX_NONE;
                return;
            }

            if(s_init_retry < GP8630_INIT_RETRY_MAX)
            {
                s_init_retry++;
                LOGW("gp8630 scan fail, retry %u\r\n", (unsigned)s_init_retry);
                GP8630_RestartProbe();
                return;
            }
        }

        LOGE("gp8630 tx fail kind=%u addr=0x%02X\r\n",
             (unsigned)s_tx_kind, (unsigned)s_i2c_addr);
        s_sm = G_SM_FAIL;
        s_init_st = GP8630_INIT_FAIL;
        s_ready = 0;
        s_tx_kind = G_TX_NONE;
        return;
    }

    if(s_tx_kind == G_TX_PROBE)
    {
        s_i2c_addr = s_probe_addr;
        LOGI("gp8630 found 0x%02X\r\n", (unsigned)s_i2c_addr);
        s_sm = G_SM_SET_MODE;
    }
    else if(s_tx_kind == G_TX_MODE)
    {
        LOGI("gp8630 mode set %u\r\n", (unsigned)s_mode);
        s_sm = G_SM_ZERO;
    }
    else if(s_tx_kind == G_TX_DATA)
    {
        if(s_sm == G_SM_ZERO)
        {
            s_sm = G_SM_READY;
            s_init_st = GP8630_INIT_OK;
            s_ready = 1;
            LOGI("gp8630 init ok\r\n");
        }
    }

    s_tx_kind = G_TX_NONE;
}

static uint8_t GP8630_SubmitReg(uint8_t reg, const uint8_t *payload, uint8_t len, GP8630_TxKind_t kind)
{
    uint8_t buf[4];
    uint8_t i;

    if(!I2C_Bus_IsIdle())
        return 1;

    buf[0] = reg;
    for(i = 0; i < len; i++)
        buf[i + 1] = payload[i];

    s_tx_kind = kind;
    if(I2C_Bus_SubmitWrite(s_i2c_addr, buf, (uint8_t)(len + 1), GP8630_OnTxDone, 0))
    {
        s_tx_kind = G_TX_NONE;
        return 1;
    }
    return 0;
}

static uint8_t GP8630_ModeToCmd(GP8630_OutputMode_t mode)
{
    switch(mode)
    {
    case GP8630_OUT_VOLT_10V:
    case GP8630_OUT_VOLT_12V:
        return GP8630_MODE_VOLT_12V;
    case GP8630_OUT_CURR_20MA:
    case GP8630_OUT_CURR_24MA:
    default:
        return GP8630_MODE_CURR_24MA;
    }
}

static uint32_t GP8630_ModeMaxScale(void)
{
    switch(s_mode)
    {
    case GP8630_OUT_VOLT_10V:
    case GP8630_OUT_VOLT_12V:
        return 12000U;
    case GP8630_OUT_CURR_20MA:
    case GP8630_OUT_CURR_24MA:
    default:
        return 24000U;
    }
}

void GP8630_BeginInitWithMode(GP8630_OutputMode_t mode)
{
    s_init_retry = 0;
    s_pending = 0;
    s_mode = mode;
    GP8630_RestartProbe();
    LOGI("gp8630 init begin mode=%u\r\n", (unsigned)s_mode);
}

void GP8630_BeginInit(void)
{
    GP8630_BeginInitWithMode(GP8630_OUT_VOLT_10V);
}

void GP8630_Update(void)
{
    uint8_t cmd;
    uint8_t payload[2];
    uint16_t code;

    if(s_sm == G_SM_IDLE || s_sm == G_SM_READY || s_sm == G_SM_FAIL)
    {
        if(s_sm == G_SM_READY && s_pending && s_tx_kind == G_TX_NONE && I2C_Bus_IsIdle())
        {
            code = s_pending_code;
            s_pending = 0;
            payload[0] = (uint8_t)(code & 0xFFU);
            payload[1] = (uint8_t)(code >> 8);
            if(GP8630_SubmitReg(GP8630_REG_DATA, payload, 2, G_TX_DATA))
            {
                s_pending = 1;
                LOGE("gp8630 output submit fail\r\n");
            }
        }
        return;
    }

    if(!I2C_Bus_IsIdle() || s_tx_kind != G_TX_NONE)
        return;

    switch(s_sm)
    {
    case G_SM_PROBE:
        s_tx_kind = G_TX_PROBE;
        if(I2C_Bus_SubmitProbe(s_probe_addr, GP8630_OnTxDone, 0))
        {
            s_tx_kind = G_TX_NONE;
            s_sm = G_SM_FAIL;
            s_init_st = GP8630_INIT_FAIL;
            LOGE("gp8630 probe submit fail\r\n");
        }
        break;

    case G_SM_SET_MODE:
        cmd = GP8630_ModeToCmd(s_mode);
        if(GP8630_SubmitReg(GP8630_REG_MODE, &cmd, 1, G_TX_MODE))
        {
            s_sm = G_SM_FAIL;
            s_init_st = GP8630_INIT_FAIL;
            LOGE("gp8630 mode submit fail\r\n");
        }
        break;

    case G_SM_ZERO:
        s_pending_code = 0U;
        payload[0] = (uint8_t)(s_pending_code & 0xFFU);
        payload[1] = (uint8_t)(s_pending_code >> 8);
        if(GP8630_SubmitReg(GP8630_REG_DATA, payload, 2, G_TX_DATA))
        {
            s_sm = G_SM_FAIL;
            s_init_st = GP8630_INIT_FAIL;
            LOGE("gp8630 zero submit fail\r\n");
        }
        break;

    default:
        break;
    }
}

GP8630_InitState_t GP8630_GetInitState(void)
{
    return s_init_st;
}

const char *GP8630_GetInitStateStr(void)
{
    switch(s_init_st)
    {
    case GP8630_INIT_BUSY:
        return "BUSY";
    case GP8630_INIT_OK:
        return "OK";
    case GP8630_INIT_FAIL:
        return "FAIL";
    default:
        return "IDLE";
    }
}

uint8_t GP8630_GetI2cAddr(void)
{
    return s_i2c_addr;
}

uint8_t GP8630_IsReady(void)
{
    return s_ready;
}

uint8_t GP8630_SetMode(GP8630_OutputMode_t mode)
{
    (void)mode;
    return 1;
}

GP8630_OutputMode_t GP8630_GetMode(void)
{
    return s_mode;
}

void GP8630_RequestPercent(uint16_t permille)
{
    uint32_t code;

    if(permille > 1000U)
        permille = 1000U;
    code = (uint32_t)permille * GP8630_CODE_MAX / 1000U;
    GP8630_RequestCode((uint16_t)code);
}

void GP8630_RequestCode(uint16_t code)
{
    if(!s_ready)
        return;

    s_pending_code = code;
    s_pending = 1;
}

uint8_t GP8630_SetVoltage_mV(uint32_t mv)
{
    if(!s_ready)
        return 1;

    GP8630_RequestPercent((uint16_t)((mv * 1000U) / GP8630_ModeMaxScale()));
    return 0;
}

uint8_t GP8630_SetCurrent_uA(uint32_t ua)
{
    if(!s_ready)
        return 1;

    GP8630_RequestPercent((uint16_t)((ua * 1000U) / GP8630_ModeMaxScale()));
    return 0;
}
