#include "Bsp/encoder.h"
#include "Bsp/buzz.h"
#include "Sys/power.h"
#include "debug.h"

#define ENC_BTN_DEBOUNCE_CNT    6U
#define ENC_BTN_LONG_CNT        160U

static volatile int16_t s_enc_diff;
static volatile uint8_t s_enc_enable = 1;
static volatile uint8_t s_enc_diff_disable;

static uint8_t s_btn_click;
static uint8_t s_btn_long;
static uint8_t s_btn_long_sent;
static uint8_t s_btn_stable;
static uint8_t s_btn_filter;
static uint8_t s_btn_cnt;
static uint16_t s_btn_hold_cnt;

static uint8_t Encoder_ReadA(void)
{
    return (GPIO_ReadInputDataBit(ENC_GPIO_A, ENC_PIN_A) == Bit_SET) ? 1 : 0;
}

static uint8_t Encoder_ReadB(void)
{
    return (GPIO_ReadInputDataBit(ENC_GPIO_B, ENC_PIN_B) == Bit_SET) ? 1 : 0;
}

static uint8_t Encoder_ReadPush(void)
{
    return (GPIO_ReadInputDataBit(ENC_GPIO_PUSH, ENC_PIN_PUSH) == Bit_RESET) ? 1 : 0;
}

static void Encoder_OnEdge(void)
{
    static int count;
    static int count_last;
    static uint8_t a0;
    static uint8_t b0;
    static uint8_t ab0;
    uint8_t a;
    uint8_t b;

    if(!s_enc_enable || s_enc_diff_disable)
        return;

    a = Encoder_ReadA();
    b = Encoder_ReadB();

    if(a != a0)
    {
        a0 = a;
        if(b != b0)
        {
            b0 = b;
            count += (a == b) ? 1 : -1;
            if((a == b) != ab0)
                count += (a == b) ? 1 : -1;
            ab0 = (a == b);
        }
    }

    if(count != count_last)
    {
        s_enc_diff += (count > count_last) ? 1 : -1;
        count_last = count;
        Power_Touch();
    }
}

void Encoder_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    EXTI_InitTypeDef exti = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOD, ENABLE);

    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;

    gpio.GPIO_Pin = ENC_PIN_PUSH;
    GPIO_Init(ENC_GPIO_PUSH, &gpio);

    gpio.GPIO_Pin = ENC_PIN_A | ENC_PIN_B;
    GPIO_Init(GPIOD, &gpio);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOD, GPIO_PinSource4);
    exti.EXTI_Line = EXTI_Line4;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    nvic.NVIC_IRQChannel = EXTI7_0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    s_btn_stable = Encoder_ReadPush();
    s_btn_filter = s_btn_stable;
    s_btn_cnt = ENC_BTN_DEBOUNCE_CNT;
    s_btn_hold_cnt = 0;
    s_btn_long_sent = 0;
}

void Encoder_Update(void)
{
    uint8_t push = Encoder_ReadPush();

    if(push != s_btn_filter)
    {
        s_btn_filter = push;
        s_btn_cnt = 0;
        return;
    }

    if(s_btn_cnt < ENC_BTN_DEBOUNCE_CNT)
    {
        s_btn_cnt++;
        return;
    }

    if(push == s_btn_stable)
    {
        if(push && !s_btn_long_sent)
        {
            if(s_btn_hold_cnt < ENC_BTN_LONG_CNT)
            {
                s_btn_hold_cnt++;
            }
            else
            {
                s_btn_long = 1;
                s_btn_long_sent = 1;
                Buzz_Tone(900, 30);
                Power_Touch();
            }
        }
        return;
    }

    if(push)
    {
        s_enc_diff_disable = 1;
        s_btn_hold_cnt = 0;
        s_btn_long_sent = 0;
        Buzz_Tone(500, 20);
    }
    else
    {
        s_enc_diff_disable = 0;
        if(!s_btn_long_sent)
            s_btn_click = 1;
        s_btn_hold_cnt = 0;
        Buzz_Tone(700, 20);
    }

    s_btn_stable = push;
    Power_Touch();
}

int16_t Encoder_GetDiff(void)
{
    int16_t diff;
    int16_t raw;

    __disable_irq();
    raw = s_enc_diff;
    diff = (int16_t)(raw / 2);
    if(diff != 0)
        s_enc_diff = 0;
    __enable_irq();

    if(diff != 0)
    {
        Buzz_Tone(300, 5);
    }

    return diff;
}

uint8_t Encoder_IsPressed(void)
{
    if(!s_enc_enable)
        return 0;
    return s_btn_stable;
}

uint8_t Encoder_GetClick(void)
{
    uint8_t click = s_btn_click;

    s_btn_click = 0;
    return click;
}

uint8_t Encoder_GetLongClick(void)
{
    uint8_t click = s_btn_long;

    s_btn_long = 0;
    return click;
}

void Encoder_SetEnable(uint8_t en)
{
    s_enc_enable = en ? 1 : 0;
}

void Encoder_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line4) != RESET)
    {
        Encoder_OnEdge();
        EXTI_ClearITPendingBit(EXTI_Line4);
    }
}
