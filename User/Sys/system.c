#include "Sys/system.h"
#include "Sys/power.h"
#include "App/app.h"
#include "Bsp/encoder.h"
#include "Bsp/buzz.h"
#include "Bsp/i2c_bus.h"
#include "Drv/gp8630.h"
#include "Sys/log.h"
#include "ch32v00x.h"

#define SYS_TICK_HZ         1000U
#define TASK_HAL_MS         5U
#define TASK_APP_MS         20U
#define TASK_HB_MS          1000U
#define SYS_UART_BAUD       115200U

static volatile uint32_t s_tick_ms;

static uint16_t System_UartBrr(uint32_t baudrate)
{
    uint32_t integerdivider;
    uint32_t fractionaldivider;
    uint32_t tmpreg;

    integerdivider = (25U * SystemCoreClock) / (4U * baudrate);
    tmpreg = (integerdivider / 100U) << 4;
    fractionaldivider = integerdivider - (100U * (tmpreg >> 4));
    tmpreg |= (((fractionaldivider * 16U) + 50U) / 100U) & 0x0FU;
    return (uint16_t)tmpreg;
}

static void System_UartInit(void)
{
    RCC->APB2PCENR |= RCC_IOPDEN | RCC_USART1EN;

    GPIOD->CFGLR = (GPIOD->CFGLR & ~(((uint32_t)0xFU << (5U * 4U)) |
                                     ((uint32_t)0xFU << (6U * 4U))))
                 | ((uint32_t)0xBU << (5U * 4U))
                 | ((uint32_t)0x4U << (6U * 4U));

    USART1->CTLR1 = 0;
    USART1->CTLR2 = 0;
    USART1->CTLR3 = 0;
    USART1->BRR = System_UartBrr(SYS_UART_BAUD);
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE |
                    USART_CTLR1_RE | USART_CTLR1_RXNEIE;
    NVIC_EnableIRQ(USART1_IRQn);
}

static void System_TickInit(void)
{
    TIM_TimeBaseInitTypeDef tim = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    tim.TIM_Period = (uint16_t)(SystemCoreClock / SYS_TICK_HZ - 1U);
    tim.TIM_Prescaler = 0;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM2, ENABLE);
}

static void Task_HAL_5ms(void)
{
    Encoder_Update();
    Buzz_Update(TASK_HAL_MS);
}

static void Task_App_20ms(void)
{
    App_Update();
}

void System_Init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    SystemCoreClockUpdate();
    System_UartInit();

    LOGI("AOMaster boot clk=%lu\r\n", (unsigned long)SystemCoreClock);

    System_TickInit();
    I2C_Bus_Init();
    Power_Init();
    Buzz_Init();
    Encoder_Init();
    App_Begin();

    LOGI("system init done\r\n");
}

void System_Run(void)
{
    uint32_t next_hal = 0;
    uint32_t next_app = 0;
    uint32_t next_hb = 0;

    while(1)
    {
        uint32_t now = s_tick_ms;

        I2C_Bus_Poll();
        GP8630_Update();
        App_SerialPoll();

        if((int32_t)(now - next_hal) >= 0)
        {
            Task_HAL_5ms();
            next_hal = now + TASK_HAL_MS;
        }

        if((int32_t)(now - next_app) >= 0)
        {
            Task_App_20ms();
            next_app = now + TASK_APP_MS;
        }

        if((int32_t)(now - next_hb) >= 0)
        {
            LOGI("hb app=%s gp=%s addr=0x%02X i2c=%u err=%lu\r\n",
                 App_GetStateStr(),
                 GP8630_GetInitStateStr(),
                 (unsigned)GP8630_GetI2cAddr(),
                 (unsigned)I2C_Bus_IsIdle(),
                 (unsigned long)I2C_Bus_GetErrCount());
            next_hb = now + TASK_HB_MS;
        }

        if(App_GetState() == APP_ST_RUN && I2C_Bus_IsIdle())
            Power_Idle();
    }
}

uint32_t System_GetTick(void)
{
    return s_tick_ms;
}

void System_UartWrite(const uint8_t *data, uint8_t len)
{
    uint8_t i;

    for(i = 0; i < len; i++)
    {
        while((USART1->STATR & USART_STATR_TXE) == 0)
        {
        }
        USART1->DATAR = data[i];
    }

    while((USART1->STATR & USART_STATR_TC) == 0)
    {
    }
}

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM2_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        s_tick_ms++;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}
