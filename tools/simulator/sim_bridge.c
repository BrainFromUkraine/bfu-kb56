/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * WebAssembly bridge for the browser simulator.
 * It links the unmodified keyboard core (src/app) and adds only what the STM32 layer
 * would do around it: the 1 ms tick, the Stop-mode rule from power.c, the LED rule from
 * led.c and an I2C master that performs transactions exactly like the host tests do.
 */
#include <string.h>
#include "app.h"

#define EXPORT __attribute__((visibility("default")))

/* ------------------------------------------------------------ freestanding libc bits */
void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++) {
        if (*p != *q) return *p < *q ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------ platform hooks */
void app_lock(void) {}
void app_unlock(void) {}

static app_t A;
static uint32_t now_ms;
static uint8_t stopped;             /* 1 = MCU in Stop 1 (power.c rule) */
static uint32_t wakeups, stops;
static uint8_t io[256];             /* I2C data buffer shared with JavaScript */
static uint32_t st[32];             /* state snapshot for JavaScript */

#define EVLOG 64
static uint8_t evlog[EVLOG][UAKB_EVENT_SIZE];
static uint32_t evlog_t[EVLOG];
static uint32_t evlog_n;

void app_event_hook(const uint8_t ev[UAKB_EVENT_SIZE])
{
    memcpy(evlog[evlog_n % EVLOG], ev, UAKB_EVENT_SIZE);
    evlog_t[evlog_n % EVLOG] = now_ms;
    evlog_n++;
}

/* power.c: may_stop() with an always-idle I2C peripheral between transactions */
static int may_stop(void)
{
    return (A.config & UAKB_CFG_SLEEP)
        && A.backlight == 0
        && A.pending == PENDING_NONE
        && !app_busy(&A)
        && (uint32_t)(now_ms - A.last_activity_ms) >= 100u;
}

static void wake(void)
{
    if (stopped) {
        stopped = 0;
        wakeups++;
        A.last_activity_ms = now_ms;        /* power.c: stay awake at least 100 ms */
    }
}

/* ------------------------------------------------------------ exports */
EXPORT void sim_init(void)
{
    app_init(&A);
    now_ms = 0;
    stopped = 0;
    wakeups = stops = 0;
    evlog_n = 0;
}

EXPORT uint8_t *sim_io(void) { return io; }

/* Run n milliseconds with the raw matrix bitmap (lo = keys 0..31, hi = keys 32..59). */
EXPORT void sim_tick(uint32_t lo, uint32_t hi, uint32_t n)
{
    const uint64_t raw = ((uint64_t)hi << 32) | lo;
    while (n--) {
        if (stopped) {
            if (raw) {                      /* a column went low: EXTI wake-up */
                wake();
            } else {
                now_ms++;
                continue;
            }
        }
        app_tick(&A, raw, now_ms);
        now_ms++;
        if (may_stop()) {
            stopped = 1;
            stops++;
        }
    }
}

/* Master write: io[0] = register pointer, io[1..len-1] = data. */
EXPORT void sim_i2c_write(uint32_t len)
{
    wake();                                 /* address match wakes the MCU (WUPEN) */
    app_i2c_write_begin(&A);
    for (uint32_t i = 0; i < len && i < sizeof(io); i++) {
        app_i2c_write_byte(&A, io[i]);
    }
    app_i2c_end(&A, false, 0);
}

/* Master read of len bytes into io[]; the slave preloads one more byte that is never sent. */
EXPORT void sim_i2c_read(uint32_t len)
{
    wake();
    app_i2c_read_begin(&A);
    for (uint32_t i = 0; i < len && i < sizeof(io); i++) {
        io[i] = app_i2c_read_byte(&A);
    }
    (void)app_i2c_read_byte(&A);
    app_i2c_end(&A, true, 1);
}

/* Takes the pending action the firmware would run after STOP (1 reset, 2 bootloader). */
EXPORT uint32_t sim_take_pending(void)
{
    const uint32_t p = (uint32_t)A.pending;
    A.pending = PENDING_NONE;
    return p;
}

static uint8_t mods_now(void)
{
    uint8_t m = 0;
    if (A.stable & (1ull << POS_SHIFT)) m |= UAKB_MOD_SHIFT;
    if (A.stable & (1ull << POS_CTRL))  m |= UAKB_MOD_CTRL;
    if (A.stable & (1ull << POS_ALT))   m |= UAKB_MOD_ALT;
    if (A.stable & (1ull << POS_FN))    m |= UAKB_MOD_FN;
    return m;
}

EXPORT uint32_t *sim_state(void)
{
    st[0]  = now_ms;
    st[1]  = app_int_asserted(&A);
    st[2]  = A.lang_ua;
    st[3]  = A.caps;
    st[4]  = A.led_mode == UAKB_LED_ON || (A.led_mode == UAKB_LED_AUTO && A.lang_ua);
    st[5]  = A.backlight;
    st[6]  = A.config;
    st[7]  = A.repeat_delay;
    st[8]  = A.repeat_rate;
    st[9]  = fifo_count(&A.fifo);
    st[10] = A.overflow;
    st[11] = stopped;
    st[12] = app_busy(&A);
    st[13] = (uint32_t)A.stable;
    st[14] = (uint32_t)(A.stable >> 32);
    st[15] = A.last_activity_ms;
    st[16] = mods_now();
    st[17] = A.caps_arm;
    st[18] = (uint32_t)(int32_t)A.repeat_pos;
    st[19] = A.led_mode;
    st[20] = evlog_n;
    st[21] = wakeups;
    st[22] = stops;
    st[23] = A.ptr;
    return st;
}

/* i-th oldest FIFO event into io[0..7] (what the host would read, without popping). */
EXPORT void sim_fifo_peek(uint32_t i)
{
    for (uint8_t b = 0; b < UAKB_EVENT_SIZE; b++) {
        io[b] = fifo_peek(&A.fifo, (uint8_t)i, b);
    }
}

/* Event log entry n (absolute index) into io[0..7]; returns its time stamp. */
EXPORT uint32_t sim_evlog(uint32_t n)
{
    memcpy(io, evlog[n % EVLOG], UAKB_EVENT_SIZE);
    return evlog_t[n % EVLOG];
}

/* Layout lookup straight from the firmware tables, for the key legends. */
EXPORT uint32_t sim_resolve(uint32_t pos, uint32_t lang_ua, uint32_t shift, uint32_t fn,
                            uint32_t caps, uint32_t apos_ascii)
{
    return keymap_resolve((uint8_t)pos, lang_ua != 0, shift != 0, fn != 0, caps != 0, apos_ascii != 0);
}

EXPORT uint32_t sim_present(uint32_t pos) { return keymap_present((uint8_t)pos); }
