/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * 5x12 key matrix. Rows PB0..PB4 are push-pull outputs, idle high; the scanned row is
 * driven low. Columns are inputs with internal pull-ups. Current path for a pressed key: column pull-up -> switch -> diode -> low row.
 */
#include "hw.h"
#include "gpio.h"

#if HAS_MATRIX

static const pin_t COLS[UAKB_COLS] = COLUMN_PINS;

#define ROW_MASK    (0x1Fu << ROW_FIRST_PIN)
#define SETTLE_LOOPS 20u    /* ~17 us at 16 MHz (13 cycles per pass); column recovery is < 2 us worst case */

static void settle(void)
{
    for (volatile uint32_t i = 0; i < SETTLE_LOOPS; i++) {
        __NOP();
    }
}

void matrix_init(void)
{
    for (unsigned c = 0; c < UAKB_COLS; c++) {
        gpio_pull(COLS[c], GPIO_PULL_UP);
        gpio_mode(COLS[c], GPIO_MODE_INPUT);
    }
    ROW_PORT->BSRR = ROW_MASK;                          /* idle high */
    for (unsigned r = 0; r < UAKB_ROWS; r++) {
        const pin_t p = { ROW_PORT, (uint8_t)(ROW_FIRST_PIN + r) };
        gpio_open_drain(p, 0);
        gpio_mode(p, GPIO_MODE_OUTPUT);
    }
}

uint64_t matrix_read(void)
{
    uint64_t raw = 0;
    for (unsigned r = 0; r < UAKB_ROWS; r++) {
        ROW_PORT->BSRR = (1u << (ROW_FIRST_PIN + r + 16u));     /* row r low */
        settle();
        for (unsigned c = 0; c < UAKB_COLS; c++) {
            if (!gpio_read(COLS[c])) {
                raw |= 1ull << (r * UAKB_COLS + c);
            }
        }
        ROW_PORT->BSRR = (1u << (ROW_FIRST_PIN + r));           /* back high */
    }
    settle();
    return raw;
}

static uint32_t port_code(const GPIO_TypeDef *port)
{
    return port == GPIOA ? 0u : port == GPIOB ? 1u : 2u;
}

void matrix_prepare_sleep(void)
{
    ROW_PORT->BSRR = (ROW_MASK << 16);                  /* all rows low: any key pulls its column low */
    settle();
    for (unsigned c = 0; c < UAKB_COLS; c++) {
        const uint32_t line = COLS[c].pin;
        volatile uint32_t *cr = &EXTI->EXTICR[line >> 2];
        const uint32_t sh = (line & 3u) * 8u;
        *cr = (*cr & ~(0xFFu << sh)) | (port_code(COLS[c].port) << sh);
        EXTI->FTSR1 |= (1u << line);
        EXTI->RTSR1 &= ~(1u << line);
        EXTI->FPR1 = (1u << line);
        EXTI->IMR1 |= (1u << line);
    }
    NVIC_ClearPendingIRQ(EXTI0_1_IRQn);
    NVIC_ClearPendingIRQ(EXTI2_3_IRQn);
    NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
}

bool matrix_any_column_low(void)
{
    for (unsigned c = 0; c < UAKB_COLS; c++) {
        if (!gpio_read(COLS[c])) {
            return true;
        }
    }
    return false;
}

void matrix_after_wake(void)
{
    for (unsigned c = 0; c < UAKB_COLS; c++) {
        EXTI->IMR1 &= ~(1u << COLS[c].pin);
        EXTI->FPR1 = (1u << COLS[c].pin);
    }
    ROW_PORT->BSRR = ROW_MASK;                          /* idle high */
}

static void exti_clear_columns(void)
{
    uint32_t mask = 0;
    for (unsigned c = 0; c < UAKB_COLS; c++) {
        mask |= 1u << COLS[c].pin;
    }
    EXTI->FPR1 = mask;
}

void EXTI0_1_IRQHandler(void)  { exti_clear_columns(); }
void EXTI2_3_IRQHandler(void)  { exti_clear_columns(); }
void EXTI4_15_IRQHandler(void) { exti_clear_columns(); }

#else

void matrix_init(void) {}
uint64_t matrix_read(void) { return 0; }
void matrix_prepare_sleep(void) {}
bool matrix_any_column_low(void) { return false; }
void matrix_after_wake(void) {}

#endif
