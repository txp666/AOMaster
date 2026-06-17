#include "Bsp/buzz.h"
#include "debug.h"

#define BUZZ_TICK_HZ    1000000U

static uint8_t s_enable = 1;
static volatile uint8_t s_active;
static volatile uint8_t s_level;
static uint32_t s_remain_ms;

static void Buzz_SetFreq(uint32_t freq)
{
    uint32_t arr;

    if(freq == 0)
    {
        TIM_Cmd(TIM1, DISABLE);
        GPIO_ResetBits(BUZZ_GPIO, BUZZ_PIN);
        s_level = 0;
        return;
    }

    arr = BUZZ_TICK_HZ / (freq * 2U);
    if(arr < 2)
        arr = 2;

    TIM_SetAutoreload(TIM1, (uint16_t)(arr - 1));
    TIM_SetCounter(TIM1, 0);
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
    TIM_Cmd(TIM1, ENABLE);
}

static void Buzz_Stop(void)
{
    TIM_Cmd(TIM1, DISABLE);
    GPIO_ResetBits(BUZZ_GPIO, BUZZ_PIN);
    s_level = 0;
    s_active = 0;
    s_remain_ms = 0;
}

void Buzz_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_TimeBaseInitTypeDef tim = {0};
    NVIC_InitTypeDef nvic = {0};
    uint16_t psc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_TIM1, ENABLE);

    gpio.GPIO_Pin = BUZZ_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(BUZZ_GPIO, &gpio);
    GPIO_ResetBits(BUZZ_GPIO, BUZZ_PIN);

    psc = (uint16_t)(SystemCoreClock / BUZZ_TICK_HZ);
    if(psc == 0)
        psc = 1;
    psc--;

    tim.TIM_Period = 1000 - 1;
    tim.TIM_Prescaler = psc;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    tim.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim);

    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM1_UP_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    Buzz_Stop();
}

void Buzz_SetEnable(uint8_t en)
{
    s_enable = en ? 1 : 0;
    if(!s_enable)
        Buzz_Stop();
}

void Buzz_Tone(uint32_t freq, uint32_t duration_ms)
{
    if(!s_enable)
        return;

    if(freq == 0)
    {
        Buzz_Stop();
        return;
    }

    /* Ignore a new short beep while one is active to mask key bounce. */
    if(s_active && duration_ms <= 30U)
        return;

    Buzz_SetFreq(freq);
    s_remain_ms = duration_ms;
    s_active = (duration_ms > 0) ? 1 : 0;
}

void Buzz_Update(uint32_t elapsed_ms)
{
    if(!s_active)
        return;

    if(s_remain_ms <= elapsed_ms)
        Buzz_Stop();
    else
        s_remain_ms -= elapsed_ms;
}

void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM1_UP_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET)
    {
        if(s_active)
        {
            if(s_level)
            {
                GPIO_ResetBits(BUZZ_GPIO, BUZZ_PIN);
                s_level = 0;
            }
            else
            {
                GPIO_SetBits(BUZZ_GPIO, BUZZ_PIN);
                s_level = 1;
            }
        }
        else
        {
            GPIO_ResetBits(BUZZ_GPIO, BUZZ_PIN);
            s_level = 0;
        }

        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
    }
}
