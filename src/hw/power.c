/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * Stop 1 after 100 ms of idle. Wake-up sources: any column (EXTI falling edge, rows are
 * held low while asleep) or an I2C address match (I2C1 WUPEN, HSI16 kernel clock).
 * GPIO outputs keep their state in Stop, so INT and the UA LED stay valid while asleep.
 */
#include "hw.h"

#define IDLE_BEFORE_STOP_MS   100u

void power_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_PWREN;
    (void)RCC->APBENR1;
#ifdef DEBUG_KEEP_SWD_IN_STOP
    /* debug builds: keep the SWD link alive in Stop */
    RCC->APBENR1 |= RCC_APBENR1_DBGEN;
    DBG->CR |= DBG_CR_DBG_STOP;
#endif
}

#if HAS_MATRIX
static bool may_stop(uint32_t now)
{
    return (g_app.config & UAKB_CFG_SLEEP)
        && g_app.backlight == 0                 /* PWM stops in Stop mode */
        && g_app.pending == PENDING_NONE
        && !app_busy(&g_app)
        && !i2c_slave_busy()
        && (uint32_t)(now - g_app.last_activity_ms) >= IDLE_BEFORE_STOP_MS;
}

#endif

void power_idle(uint32_t now)
{
#if !HAS_MATRIX
    (void)now;
    __WFI();                                    /* test build: never enters Stop */
#else
    if (!may_stop(now)) {
        __WFI();                                /* Sleep until the next 1 ms tick or I2C */
        return;
    }

    matrix_prepare_sleep();
    if (matrix_any_column_low()) {              /* a key went down meanwhile */
        matrix_after_wake();
        return;
    }

    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS) | PWR_CR1_LPMS_0;     /* Stop 1 */
    __disable_irq();                            /* close the race with a starting I2C transfer */
    if (!i2c_slave_busy() && !matrix_any_column_low()) {
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
        __DSB();
        __WFI();                                /* a pending IRQ still wakes the core */
        SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    }
    __enable_irq();
    SysTick->VAL = 0;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

    matrix_after_wake();
    g_app.last_activity_ms = g_ms;              /* stay awake at least 100 ms */
#endif
}
