/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * UA indicator LED and the optional backlight PWM (TIM16_CH1, ~3.9 kHz, 256 steps).
 */
#include "hw.h"
#include "gpio.h"

static const pin_t LED = LED_UA_PIN;

#if HAS_BACKLIGHT
static const pin_t BL = BL_PWM_PIN;
static uint8_t s_bl = 0xFFu;    /* force first update */
#endif

void led_init(void)
{
    gpio_clear(LED);
    gpio_open_drain(LED, 0);
    gpio_mode(LED, GPIO_MODE_OUTPUT);

#if HAS_BACKLIGHT
    RCC->APBENR2 |= RCC_APBENR2_TIM16EN;
    (void)RCC->APBENR2;
    TIM16->PSC = (SYSCLK_HZ / 1000000u) - 1u;           /* 1 MHz */
    TIM16->ARR = 255u;
    TIM16->CCR1 = 0;
    TIM16->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;   /* PWM mode 1 */
    TIM16->CCER = TIM_CCER_CC1E;
    TIM16->BDTR = TIM_BDTR_MOE;
    TIM16->EGR = TIM_EGR_UG;
    TIM16->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    gpio_clear(BL);
    gpio_af(BL, BL_PWM_AF);
    gpio_mode(BL, GPIO_MODE_AF);
#endif
}

void led_update(void)
{
    bool on;
    switch (g_app.led_mode) {
    case UAKB_LED_ON:  on = true; break;
    case UAKB_LED_OFF: on = false; break;
    default:           on = g_app.lang_ua != 0; break;
    }
    if (on) gpio_set(LED);
    else    gpio_clear(LED);

#if HAS_BACKLIGHT
    if (g_app.backlight != s_bl) {
        s_bl = g_app.backlight;
        TIM16->CCR1 = (s_bl == 255u) ? 256u : s_bl;     /* 255 = fully on */
    }
#endif
}
