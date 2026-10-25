/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef UAKB_GPIO_H
#define UAKB_GPIO_H

#include "board.h"

#define GPIO_MODE_INPUT     0u
#define GPIO_MODE_OUTPUT    1u
#define GPIO_MODE_AF        2u
#define GPIO_MODE_ANALOG    3u

#define GPIO_PULL_NONE      0u
#define GPIO_PULL_UP        1u
#define GPIO_PULL_DOWN      2u

static inline void gpio_clock_on(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;
    (void)RCC->IOPENR;
}

static inline void gpio_mode(pin_t p, uint32_t mode)
{
    p.port->MODER = (p.port->MODER & ~(3u << (p.pin * 2u))) | (mode << (p.pin * 2u));
}

static inline void gpio_pull(pin_t p, uint32_t pull)
{
    p.port->PUPDR = (p.port->PUPDR & ~(3u << (p.pin * 2u))) | (pull << (p.pin * 2u));
}

static inline void gpio_open_drain(pin_t p, int od)
{
    if (od) p.port->OTYPER |= (1u << p.pin);
    else    p.port->OTYPER &= ~(1u << p.pin);
}

static inline void gpio_af(pin_t p, uint32_t af)
{
    volatile uint32_t *reg = &p.port->AFR[p.pin >> 3];
    const uint32_t sh = (p.pin & 7u) * 4u;
    *reg = (*reg & ~(0xFu << sh)) | (af << sh);
}

static inline void gpio_set(pin_t p)   { p.port->BSRR = (1u << p.pin); }
static inline void gpio_clear(pin_t p) { p.port->BSRR = (1u << (p.pin + 16u)); }
static inline int  gpio_read(pin_t p)  { return (p.port->IDR >> p.pin) & 1u; }

#endif
