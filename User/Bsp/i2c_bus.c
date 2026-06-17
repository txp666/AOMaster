#include "Bsp/i2c_bus.h"
#include "Sys/log.h"
#include "ch32v00x.h"

#define OLED_I2C_CLKRATE        400000U
#define OLED_I2C_SDA_PIN        GPIO_Pin_1
#define OLED_I2C_SCL_PIN        GPIO_Pin_2

#define I2C_ADDR_TRANSMITTED    0x00820003U
#define I2C_checkEvent(n)       (((((uint32_t)I2C1->STAR1 << 16) | I2C1->STAR2) & (n)) == (n))
#define I2C_SPIN_BUDGET         64U
#define I2C_WAIT_RETRY_MAX      400U
#define I2C_BUS_BUF_MAX         140U

typedef enum
{
    SM_IDLE = 0,
    SM_WAIT_BUSY,
    SM_START,
    SM_WAIT_SB,
    SM_ADDR,
    SM_WAIT_ADDR,
    SM_TX,
    SM_WAIT_TXE,
    SM_STOP,
    SM_WAIT_BTF,
    SM_FINISH,
} I2C_SmState_t;

typedef struct
{
    I2C_SmState_t sm;
    I2C_BusStatus_t result;
    uint8_t addr8;
    uint8_t buf[I2C_BUS_BUF_MAX];
    uint8_t len;
    uint8_t idx;
    uint8_t probe_only;
    uint16_t retry;
    I2C_BusDoneCb cb;
    void *ctx;
} I2C_BusCtx_t;

static I2C_BusCtx_t s_tx;
static uint32_t s_err_count;

static void I2C_Bus_HwInit(void)
{
    uint32_t pclk;
    uint32_t ccr;
    uint32_t freq_mhz;

    RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPCEN;
    RCC->APB1PCENR |= RCC_I2C1EN;

    GPIOC->CFGLR = (GPIOC->CFGLR
                    & ~(((uint32_t)0b1111 << (1 << 2)) | ((uint32_t)0b1111 << (2 << 2))))
                 | (((uint32_t)0b1101 << (1 << 2)) | ((uint32_t)0b1101 << (2 << 2)));

    pclk = SystemCoreClock;
    freq_mhz = pclk / 1000000U;
    if(freq_mhz < 2U)
        freq_mhz = 2U;
    if(freq_mhz > 63U)
        freq_mhz = 63U;

    ccr = pclk / (3U * OLED_I2C_CLKRATE);
    if(ccr == 0U)
        ccr = 1U;

    I2C1->CTLR1 &= ~I2C_CTLR1_PE;
    I2C1->CTLR2 = (uint16_t)freq_mhz;
    I2C1->CKCFGR = (uint16_t)(ccr | I2C_CKCFGR_FS);
    I2C1->CTLR1 = I2C_CTLR1_PE;
}

static void I2C_Bus_RecoverHw(void)
{
    uint8_t i;

    I2C1->CTLR1 |= I2C_CTLR1_STOP;
    I2C1->CTLR1 &= ~I2C_CTLR1_PE;

    RCC->APB2PCENR |= RCC_IOPCEN;
    GPIOC->CFGLR = (GPIOC->CFGLR
                    & ~(((uint32_t)0b1111 << (1 << 2)) | ((uint32_t)0b1111 << (2 << 2))))
                 | (((uint32_t)0b0011 << (1 << 2)) | ((uint32_t)0b0011 << (2 << 2)));

    GPIOC->BSHR = OLED_I2C_SCL_PIN | OLED_I2C_SDA_PIN;
    for(i = 0; i < 9; i++)
    {
        GPIOC->BCR = OLED_I2C_SCL_PIN;
        GPIOC->BSHR = OLED_I2C_SCL_PIN;
    }
    GPIOC->BSHR = OLED_I2C_SDA_PIN;

    I2C_Bus_HwInit();
    LOGW("i2c bus recovered\r\n");
}

static void I2C_Bus_Finish(I2C_BusStatus_t status)
{
    I2C_BusDoneCb cb = s_tx.cb;
    void *ctx = s_tx.ctx;

    s_tx.sm = SM_IDLE;
    s_tx.result = status;
    s_tx.cb = 0;
    s_tx.ctx = 0;

    if(status == I2C_BUS_ERR)
        s_err_count++;

    if(cb)
        cb(status, ctx);
}

static void I2C_Bus_Fail(void)
{
    I2C1->CTLR1 |= I2C_CTLR1_STOP;
    if(I2C1->STAR1 & I2C_STAR1_AF)
        I2C1->STAR1 &= ~I2C_STAR1_AF;
    LOGE("i2c fail sm=%u addr=0x%02X\r\n", (unsigned)s_tx.sm, (unsigned)s_tx.addr8);
    I2C_Bus_RecoverHw();
    I2C_Bus_Finish(I2C_BUS_ERR);
}

static uint8_t I2C_Bus_SpinFlag(uint16_t flag, uint8_t expect)
{
    uint16_t n = I2C_SPIN_BUDGET;

    while(n--)
    {
        if(((I2C1->STAR1 & flag) != 0) == expect)
            return 0;
        if(I2C1->STAR1 & I2C_STAR1_AF)
            return 2;
    }
    return 1;
}

static uint8_t I2C_Bus_SpinEvent(uint32_t event)
{
    uint16_t n = I2C_SPIN_BUDGET;

    while(n--)
    {
        if(I2C_checkEvent(event))
            return 0;
        if(I2C1->STAR1 & I2C_STAR1_AF)
            return 2;
    }
    return 1;
}

void I2C_Bus_Init(void)
{
    s_tx.sm = SM_IDLE;
    s_tx.result = I2C_BUS_IDLE;
    s_err_count = 0;
    I2C_Bus_HwInit();
}

I2C_BusStatus_t I2C_Bus_GetStatus(void)
{
    return s_tx.result;
}

uint8_t I2C_Bus_IsIdle(void)
{
    return (s_tx.sm == SM_IDLE) ? 1 : 0;
}

uint32_t I2C_Bus_GetErrCount(void)
{
    return s_err_count;
}

static uint8_t I2C_Bus_Begin(uint8_t addr8, const uint8_t *data, uint8_t len,
                             uint8_t probe_only, I2C_BusDoneCb cb, void *ctx)
{
    uint8_t i;

    if(s_tx.sm != SM_IDLE)
        return 1;
    if(!probe_only && (data == 0 || len == 0 || len > I2C_BUS_BUF_MAX))
        return 1;

    s_tx.addr8 = addr8;
    s_tx.len = probe_only ? 0 : len;
    s_tx.idx = 0;
    s_tx.probe_only = probe_only;
    s_tx.retry = 0;
    s_tx.cb = cb;
    s_tx.ctx = ctx;
    s_tx.result = I2C_BUS_BUSY;

    if(!probe_only)
    {
        for(i = 0; i < len; i++)
            s_tx.buf[i] = data[i];
    }

    if(I2C1->STAR1 & I2C_STAR1_AF)
        I2C1->STAR1 &= ~I2C_STAR1_AF;

    s_tx.sm = SM_WAIT_BUSY;
    return 0;
}

uint8_t I2C_Bus_SubmitProbe(uint8_t addr8, I2C_BusDoneCb cb, void *ctx)
{
    return I2C_Bus_Begin(addr8, 0, 0, 1, cb, ctx);
}

uint8_t I2C_Bus_SubmitWrite(uint8_t addr8, const uint8_t *data, uint8_t len,
                            I2C_BusDoneCb cb, void *ctx)
{
    return I2C_Bus_Begin(addr8, data, len, 0, cb, ctx);
}

void I2C_Bus_Abort(void)
{
    if(s_tx.sm == SM_IDLE)
        return;
    I2C_Bus_Fail();
}

void I2C_Bus_Poll(void)
{
    uint8_t r;

    if(s_tx.sm == SM_IDLE)
        return;

    switch(s_tx.sm)
    {
    case SM_WAIT_BUSY:
        if(I2C1->STAR2 & I2C_STAR2_BUSY)
        {
            if(++s_tx.retry > I2C_WAIT_RETRY_MAX)
                I2C_Bus_Fail();
            return;
        }
        s_tx.retry = 0;
        s_tx.sm = SM_START;
        break;

    case SM_START:
        I2C1->CTLR1 |= I2C_CTLR1_START;
        s_tx.sm = SM_WAIT_SB;
        break;

    case SM_WAIT_SB:
        r = I2C_Bus_SpinFlag(I2C_STAR1_SB, 1);
        if(r == 1)
        {
            if(++s_tx.retry > I2C_WAIT_RETRY_MAX)
                I2C_Bus_Fail();
            return;
        }
        if(r == 2)
        {
            I2C_Bus_Fail();
            return;
        }
        s_tx.retry = 0;
        s_tx.sm = SM_ADDR;
        break;

    case SM_ADDR:
        I2C1->DATAR = s_tx.addr8;
        s_tx.sm = SM_WAIT_ADDR;
        break;

    case SM_WAIT_ADDR:
        r = I2C_Bus_SpinEvent(I2C_ADDR_TRANSMITTED);
        if(r == 1)
        {
            if(++s_tx.retry > I2C_WAIT_RETRY_MAX)
                I2C_Bus_Fail();
            return;
        }
        if(r == 2)
        {
            I2C_Bus_Fail();
            return;
        }
        s_tx.retry = 0;
        if(s_tx.probe_only || s_tx.len == 0)
        {
            /* Address-only probe: ADDR event means device ACKed.
             * There is no data byte, so BTF will never set. Issue STOP
             * and finish OK directly instead of waiting on SM_WAIT_BTF. */
            I2C1->CTLR1 |= I2C_CTLR1_STOP;
            s_tx.sm = SM_FINISH;
        }
        else
        {
            s_tx.sm = SM_TX;
        }
        break;

    case SM_TX:
        I2C1->DATAR = s_tx.buf[s_tx.idx++];
        s_tx.sm = SM_WAIT_TXE;
        break;

    case SM_WAIT_TXE:
        r = I2C_Bus_SpinFlag(I2C_STAR1_TXE, 1);
        if(r == 1)
        {
            if(++s_tx.retry > I2C_WAIT_RETRY_MAX)
                I2C_Bus_Fail();
            return;
        }
        if(r == 2)
        {
            I2C_Bus_Fail();
            return;
        }
        s_tx.retry = 0;
        if(s_tx.idx < s_tx.len)
            s_tx.sm = SM_TX;
        else
            s_tx.sm = SM_STOP;
        break;

    case SM_STOP:
        s_tx.sm = SM_WAIT_BTF;
        break;

    case SM_WAIT_BTF:
        r = I2C_Bus_SpinFlag(I2C_STAR1_BTF, 1);
        if(r == 1)
        {
            if(++s_tx.retry > I2C_WAIT_RETRY_MAX)
                I2C_Bus_Fail();
            return;
        }
        if(r == 2)
        {
            I2C_Bus_Fail();
            return;
        }
        I2C1->CTLR1 |= I2C_CTLR1_STOP;
        s_tx.sm = SM_FINISH;
        break;

    case SM_FINISH:
        s_tx.result = I2C_BUS_OK;
        I2C_Bus_Finish(I2C_BUS_OK);
        break;

    default:
        I2C_Bus_Fail();
        break;
    }
}
