/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * BFU-KB56 firmware — main loop.
 * Clock: HSI16 (reset default), SysTick 1 kHz. Everything runs from the 1 ms tick;
 * between ticks the core sleeps (WFI) or, when idle, enters Stop 1.
 */
#include "hw.h"
#include "gpio.h"

app_t g_app;
volatile uint32_t g_ms;
static volatile uint8_t s_tick;

uint32_t SystemCoreClock = SYSCLK_HZ;

void SysTick_Handler(void)
{
    g_ms++;
    s_tick = 1;
}

static uint32_t s_primask;
static uint8_t s_lock_depth;

void app_lock(void)
{
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();
    if (s_lock_depth++ == 0u) {
        s_primask = pm;
    }
}

void app_unlock(void)
{
    if (--s_lock_depth == 0u) {
        __set_PRIMASK(s_primask);
    }
}

static uint8_t read_address(void)
{
#ifdef ADDR_SEL_PIN
    const pin_t a = ADDR_SEL_PIN;
    gpio_pull(a, GPIO_PULL_UP);
    gpio_mode(a, GPIO_MODE_INPUT);
    for (volatile int i = 0; i < 200; i++) {
        __NOP();
    }
    const uint8_t addr = gpio_read(a) ? UAKB_I2C_ADDR_DEFAULT : UAKB_I2C_ADDR_ALT;
    gpio_mode(a, GPIO_MODE_ANALOG);              /* no pull-up current while running */
    gpio_pull(a, GPIO_PULL_NONE);
    return addr;
#else
    return UAKB_I2C_ADDR_DEFAULT;
#endif
}

static void handle_pending(void)
{
    if (g_app.pending == PENDING_NONE || i2c_slave_busy()) {
        return;
    }
    for (volatile int i = 0; i < 20000; i++) {   /* let the master finish (STOP seen) */
        __NOP();
    }
    if (g_app.pending == PENDING_BOOTLOADER) {
        boot_enter_system_bootloader();
    }
    NVIC_SystemReset();
}

int main(void)
{
    gpio_clock_on();
    app_init(&g_app);
    log_init();
    log_str("\r\nBFU-KB56 fw " FW_VERSION " on " BOARD_NAME "\r\n");

    matrix_init();
    led_init();
    demo_init();
    power_init();
    i2c_slave_init(read_address());
    led_update();
    int_pin_update();

    SysTick_Config(SYSCLK_HZ / 1000u);

    for (;;) {
        if (s_tick) {
            s_tick = 0;
            const uint32_t now = g_ms;
            const uint64_t raw = HAS_MATRIX ? matrix_read() : demo_raw(now);
            app_tick(&g_app, raw, now);
            app_lock();
            int_pin_update();
            app_unlock();
            led_update();
            handle_pending();
#if HAS_UART_LOG
            static uint8_t s_full_logged;
            if (fifo_count(&g_app.fifo) >= UAKB_FIFO_DEPTH && !s_full_logged) {
                s_full_logged = 1;
                log_str("FIFO full (16 events): read them over I2C, new events are dropped\r\n");
            } else if (fifo_count(&g_app.fifo) < UAKB_FIFO_DEPTH) {
                s_full_logged = 0;
            }
#endif
        }
        power_idle(g_ms);
    }
}
