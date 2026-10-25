/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * NUCLEO test build: event log on USART2 -> ST-LINK virtual COM port, 115200 8N1.
 * Each event is printed as hex plus its text, e.g.
 *   press   key 16 mods 01 st 01 [d0 9f 00 00] "П"
 */
#include "hw.h"
#include "gpio.h"

#if HAS_UART_LOG

static const pin_t TX = UART_TX_PIN;

void log_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_USART2EN;
    (void)RCC->APBENR1;
    gpio_af(TX, UART_AF);
    gpio_mode(TX, GPIO_MODE_AF);
    USART2->BRR = (SYSCLK_HZ + 115200u / 2u) / 115200u;
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void putc_(char c)
{
    while (!(USART2->ISR & USART_ISR_TXE_TXFNF)) {
    }
    USART2->TDR = (uint8_t)c;
}

void log_str(const char *s)
{
    while (*s) {
        putc_(*s++);
    }
}

static void hex8(uint8_t v)
{
    static const char H[] = "0123456789abcdef";
    putc_(H[v >> 4]);
    putc_(H[v & 15u]);
}

static void dec(uint8_t v)
{
    if (v >= 10u) putc_((char)('0' + v / 10u));
    putc_((char)('0' + v % 10u));
}

void app_event_hook(const uint8_t ev[UAKB_EVENT_SIZE])
{
    static const char *const TYPES[] = { "none   ", "press  ", "release", "repeat " };
    log_str(TYPES[ev[0] & 3u]);
    log_str(" key ");   dec(ev[1]);
    log_str(" mods ");  hex8(ev[2]);
    log_str(" st ");    hex8(ev[3]);
    log_str(" [");
    for (int i = 4; i < 8; i++) { hex8(ev[i]); if (i < 7) putc_(' '); }
    log_str("] ");
    if (ev[4] >= 0x20u) {
        putc_('"');
        for (int i = 4; i < 8 && ev[i]; i++) putc_((char)ev[i]);
        putc_('"');
    } else if (ev[4]) {
        log_str("ctrl 0x"); hex8(ev[4]);
    } else {
        log_str("special 0x"); hex8(ev[5]);
    }
    log_str("\r\n");
}

#else

void log_init(void) {}
void log_str(const char *s) { (void)s; }

#endif
