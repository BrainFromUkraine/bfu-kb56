/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * NUCLEO test build: each press of the blue USER button "types" a scripted phrase by
 * feeding synthetic matrix states into the very same keyboard core:
 *   Привіт, BFU! ʼ₴ Ґ⏎
 * It exercises Shift, the language key, the UA apostrophe/hryvnia key, Fn+Shift and Enter.
 */
#include "hw.h"
#include "gpio.h"

#if HAS_DEMO

#define K(r, c)     (1ull << UAKB_KEYCODE(r, c))
#define SHIFT       K(3, 0)
#define FN          K(4, 0)
#define LANG        K(4, 3)
#define HOLD_MS     30u
#define GAP_MS      30u

static const uint64_t SCRIPT[] = {
    SHIFT | K(2, 4),  /* П */
    K(2, 5),          /* р */
    K(3, 5),          /* и */
    K(2, 2),          /* в */
    K(2, 1),          /* і */
    K(3, 6),          /* т */
    SHIFT | K(3, 10), /* , */
    K(4, 5),          /* space */
    LANG,             /* -> EN */
    SHIFT | K(3, 5),  /* B */
    SHIFT | K(2, 3),  /* F */
    SHIFT | K(1, 6),  /* U */
    SHIFT | K(0, 1),  /* ! */
    K(4, 5),          /* space */
    LANG,             /* -> UA */
    K(0, 0),          /* ʼ */
    SHIFT | K(0, 0),  /* ₴ */
    K(4, 5),          /* space */
    FN | SHIFT | K(1, 6), /* Ґ */
    K(2, 11),         /* Enter */
};
#define STEPS (sizeof(SCRIPT) / sizeof(SCRIPT[0]))

static const pin_t BTN = BUTTON_PIN;
static int s_idle_level;
static int s_last;
static uint32_t s_changed;
static int s_playing;
static int s_fired;
static unsigned s_step;
static uint32_t s_step_start;

void demo_init(void)
{
    gpio_pull(BTN, GPIO_PULL_NONE);              /* the board has its own resistor */
    gpio_mode(BTN, GPIO_MODE_INPUT);
    s_idle_level = gpio_read(BTN);               /* works for either button polarity */
    s_last = s_idle_level;
}

uint64_t demo_raw(uint32_t now)
{
    const int lvl = gpio_read(BTN);
    if (lvl != s_last) {
        s_last = lvl;
        s_changed = now;
    }
    if (lvl == s_idle_level) {
        s_fired = 0;
    } else if (!s_fired && !s_playing && (uint32_t)(now - s_changed) >= 20u) {
        s_fired = 1;
        s_playing = 1;
        s_step = 0;
        s_step_start = now;
        log_str("demo: typing script\r\n");
    }
    if (!s_playing) {
        return 0;
    }
    const uint32_t t = now - s_step_start;
    if (t >= HOLD_MS + GAP_MS) {
        s_step_start = now;
        if (++s_step >= STEPS) {
            s_playing = 0;
            return 0;
        }
        return SCRIPT[s_step];
    }
    return (t < HOLD_MS) ? SCRIPT[s_step] : 0;
}

#else

void demo_init(void) {}
uint64_t demo_raw(uint32_t now) { (void)now; return 0; }

#endif
