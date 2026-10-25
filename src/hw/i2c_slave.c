/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * I2C1 slave, interrupt driven. Kernel clock = HSI16 so an address match can wake the
 * MCU from Stop (WUPEN). Clock stretching stays enabled, but every byte is produced
 * directly in the interrupt, so stretches are a few microseconds.
 */
#include "hw.h"
#include "gpio.h"

/* RM0444 example timing for fI2CCLK = 16 MHz, Fast-mode 400 kHz.
 * In slave mode only SDADEL/SCLDEL (data setup/hold) are used. */
#define I2C_TIMINGR_16MHZ_400K   0x10320309u

typedef enum { MODE_IDLE = 0, MODE_WRITE, MODE_READ } mode_t;
static volatile mode_t s_mode;

static const pin_t SCL = I2C_SCL, SDA = I2C_SDA, INTP = INT_PIN;

void i2c_slave_init(uint8_t addr7)
{
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_I2C1SEL) | RCC_CCIPR_I2C1SEL_1;   /* 10: HSI16 */
    RCC->APBENR1 |= RCC_APBENR1_I2C1EN;
    (void)RCC->APBENR1;

    for (unsigned i = 0; i < 2; i++) {
        const pin_t p = i ? SDA : SCL;
        gpio_open_drain(p, 1);
        gpio_pull(p, GPIO_PULL_NONE);          /* pull-ups are on the host or JP1/JP2 */
        gpio_af(p, I2C_AF);
        gpio_mode(p, GPIO_MODE_AF);
    }

    gpio_set(INTP);                            /* released */
    gpio_open_drain(INTP, 1);
    gpio_mode(INTP, GPIO_MODE_OUTPUT);

    I2C1->CR1 = 0;
    I2C1->TIMINGR = I2C_TIMINGR_16MHZ_400K;
    I2C1->OAR1 = 0;
    I2C1->OAR1 = I2C_OAR1_OA1EN | ((uint32_t)addr7 << 1);
    I2C1->CR1 = I2C_CR1_ADDRIE | I2C_CR1_RXIE | I2C_CR1_TXIE | I2C_CR1_STOPIE |
                I2C_CR1_NACKIE | I2C_CR1_ERRIE | I2C_CR1_WUPEN | I2C_CR1_PE;

    EXTI->IMR1 |= EXTI_IMR1_IM23;              /* I2C1 wakeup line */
    NVIC_SetPriority(I2C1_IRQn, 0);
    NVIC_EnableIRQ(I2C1_IRQn);
}

bool i2c_slave_busy(void)
{
    return (I2C1->ISR & I2C_ISR_BUSY) != 0 || s_mode != MODE_IDLE;
}

void int_pin_update(void)
{
    if (app_int_asserted(&g_app)) gpio_clear(INTP);
    else                          gpio_set(INTP);
}

static void finish(void)
{
    if (s_mode == MODE_IDLE) {
        return;
    }
    uint8_t unsent = 0;
    if (s_mode == MODE_READ) {
        unsent = (I2C1->ISR & I2C_ISR_TXE) ? 0u : 1u;   /* a preloaded byte never left */
        I2C1->ISR |= I2C_ISR_TXE;                       /* flush TXDR */
    }
    app_i2c_end(&g_app, s_mode == MODE_READ, unsent);
    s_mode = MODE_IDLE;
    int_pin_update();
}

void I2C1_IRQHandler(void)
{
    const uint32_t isr = I2C1->ISR;

    /* Flags are handled in bus order, so a late interrupt (several flags pending at once)
     * still sees them in the order they happened: the last data byte of a transaction (RXNE),
     * then its end (NACKF, STOPF, errors), then the next START (ADDR), then data requests. */
    if (isr & I2C_ISR_RXNE) {
        app_i2c_write_byte(&g_app, (uint8_t)I2C1->RXDR);
    }
    if (isr & I2C_ISR_NACKF) {
        I2C1->ICR = I2C_ICR_NACKCF;
    }
    if (isr & I2C_ISR_STOPF) {
        I2C1->ICR = I2C_ICR_STOPCF;
        finish();
    }
    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
        I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
        finish();
    }
    if (isr & I2C_ISR_ADDR) {
        if (s_mode != MODE_IDLE) {
            finish();                                    /* repeated START */
        }
        if (I2C1->ISR & I2C_ISR_DIR) {                    /* master reads */
            I2C1->ISR |= I2C_ISR_TXE;
            app_i2c_read_begin(&g_app);
            s_mode = MODE_READ;
        } else {
            app_i2c_write_begin(&g_app);
            s_mode = MODE_WRITE;
        }
        I2C1->ICR = I2C_ICR_ADDRCF;
    }
    if (s_mode == MODE_READ && (I2C1->ISR & I2C_ISR_TXIS)) {
        I2C1->TXDR = app_i2c_read_byte(&g_app);
    }
}
