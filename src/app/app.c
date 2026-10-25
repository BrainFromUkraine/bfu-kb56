/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "app.h"
#include "utf8.h"

#define BIT(pos)    (1ull << (pos))

static const uint8_t LIGHT_STEPS[] = { 0, 64, 128, 255 };

/* ------------------------------------------------------------------ helpers */

static uint8_t state_byte(const app_t *a)
{
    return (uint8_t)((a->lang_ua ? UAKB_STATE_LANG_UA : 0u) | (a->caps ? UAKB_STATE_CAPS : 0u));
}

static uint8_t mods_from(const app_t *a)
{
    uint8_t m = 0;
    if (a->stable & BIT(POS_SHIFT)) m |= UAKB_MOD_SHIFT;
    if (a->stable & BIT(POS_CTRL))  m |= UAKB_MOD_CTRL;
    if (a->stable & BIT(POS_ALT))   m |= UAKB_MOD_ALT;
    if (a->stable & BIT(POS_FN))    m |= UAKB_MOD_FN;
    return m;
}

__attribute__((weak)) void app_event_hook(const uint8_t ev[UAKB_EVENT_SIZE])
{
    (void)ev;
}

static void emit(app_t *a, const uint8_t ev[UAKB_EVENT_SIZE])
{
    app_lock();
    const bool ok = fifo_push(&a->fifo, ev);
    if (!ok) {
        a->overflow = 1;
    }
    app_unlock();
    if (ok) {
        app_event_hook(ev);
    }
}

static void emit_special(app_t *a, uint8_t pos, uint8_t mods, uint8_t code)
{
    uint8_t ev[UAKB_EVENT_SIZE] = { UAKB_EV_PRESS, pos, mods, state_byte(a), 0, code, 0, 0 };
    emit(a, ev);
}

static uint8_t next_light(uint8_t cur)
{
    for (unsigned i = 0; i < sizeof(LIGHT_STEPS); i++) {
        if (cur < LIGHT_STEPS[i]) {
            return LIGHT_STEPS[i];
        }
    }
    return LIGHT_STEPS[0];
}

static uint32_t ms(uint8_t ten_ms)
{
    return (uint32_t)(ten_ms ? ten_ms : 1u) * 10u;
}

/* ------------------------------------------------------------------ keys */

static void on_press(app_t *a, uint8_t pos, uint32_t now)
{
    const uint8_t mods = mods_from(a);
    const kact_t act = keymap_resolve(pos, a->lang_ua, (mods & UAKB_MOD_SHIFT) != 0,
                                      (mods & UAKB_MOD_FN) != 0, a->caps,
                                      (a->config & UAKB_CFG_APOS_ASCII) != 0);
    if (act == KA_NONE || KA_IS_MOD(act)) {
        return;
    }
    if (!(KA_IS_SPECIAL(act) && (act & 0xFFu) == UAKB_SP_CAPS)) {
        a->caps_arm = 0;
    }
    if (KA_IS_SPECIAL(act)) {
        const uint8_t code = (uint8_t)(act & 0xFFu);
        if (code == UAKB_SP_LANG) {
            a->lang_ua = !a->lang_ua;
            emit_special(a, pos, mods, code);
            return;
        }
        if (code == UAKB_SP_CAPS) {
            /* toggled on Shift release, only if no other key was pressed meanwhile */
            a->caps_arm = 1;
            return;
        }
        if (code == UAKB_SP_LIGHT) {
            a->backlight = next_light(a->backlight);
            emit_special(a, pos, mods, code);
            return;
        }
    }

    uint8_t *p = a->payload[pos];
    memset(p, 0, UAKB_EVENT_SIZE);
    p[0] = UAKB_EV_PRESS;
    p[1] = pos;
    p[2] = mods;
    p[3] = state_byte(a);
    if (KA_IS_TEXT(act)) {
        uint8_t u[4];
        (void)utf8_encode(act, u);
        memcpy(&p[4], u, 4);
    } else {
        p[5] = (uint8_t)(act & 0xFFu);
    }
    emit(a, p);
    a->reported |= BIT(pos);

    if (a->config & UAKB_CFG_REPEAT) {
        a->repeat_pos = (int16_t)pos;
        a->repeat_due = now + ms(a->repeat_delay);
    }
}

static void on_release(app_t *a, uint8_t pos)
{
    if (pos == POS_SHIFT && a->caps_arm) {
        a->caps_arm = 0;
        a->caps = !a->caps;
        emit_special(a, pos, mods_from(a), UAKB_SP_CAPS);
    }
    if (a->repeat_pos == (int16_t)pos) {
        a->repeat_pos = -1;
    }
    if (a->reported & BIT(pos)) {
        a->reported &= ~BIT(pos);
        if (a->config & UAKB_CFG_RELEASES) {
            uint8_t ev[UAKB_EVENT_SIZE];
            memcpy(ev, a->payload[pos], UAKB_EVENT_SIZE);
            ev[0] = UAKB_EV_RELEASE;
            ev[2] = mods_from(a);
            emit(a, ev);
        }
    }
}

void app_init(app_t *a)
{
    memset(a, 0, sizeof(*a));
    fifo_init(&a->fifo);
    a->repeat_pos = -1;
    a->lang_ua = 1;
    a->config = UAKB_CFG_DEFAULT;
    a->repeat_delay = UAKB_REPEAT_DELAY_DEFAULT;
    a->repeat_rate = UAKB_REPEAT_RATE_DEFAULT;
    a->led_mode = UAKB_LED_AUTO;
    a->expect_ptr = true;
}

static void debounce_key(app_t *a, uint8_t pos, uint64_t raw, uint32_t now)
{
    const bool down = (raw & BIT(pos)) != 0;
    const bool was = (a->stable & BIT(pos)) != 0;
    if (down == was) {
        a->integ[pos] = 0;
        return;
    }
    if (++a->integ[pos] < DEBOUNCE_MS) {
        return;
    }
    a->integ[pos] = 0;
    a->last_activity_ms = now;
    if (down) {
        a->stable |= BIT(pos);
        on_press(a, pos, now);
    } else {
        a->stable &= ~BIT(pos);
        on_release(a, pos);
    }
}

/* Modifiers settle first (Fn before the others, so Fn+Shift gives Caps), then the
 * remaining keys see the modifier state of the same millisecond. */
static const uint8_t MOD_ORDER[] = { POS_FN, POS_SHIFT, POS_CTRL, POS_ALT };

static bool is_mod_pos(uint8_t pos)
{
    for (unsigned i = 0; i < sizeof(MOD_ORDER); i++) {
        if (MOD_ORDER[i] == pos) {
            return true;
        }
    }
    return false;
}

void app_tick(app_t *a, uint64_t raw, uint32_t now)
{
    for (unsigned i = 0; i < sizeof(MOD_ORDER); i++) {
        debounce_key(a, MOD_ORDER[i], raw, now);
    }
    for (uint8_t pos = 0; pos < KEY_COUNT; pos++) {
        if (keymap_present(pos) && !is_mod_pos(pos)) {
            debounce_key(a, pos, raw, now);
        }
    }

    if (a->repeat_pos >= 0) {
        if (!(a->config & UAKB_CFG_REPEAT)) {
            a->repeat_pos = -1;
        } else if ((int32_t)(now - a->repeat_due) >= 0) {
            uint8_t ev[UAKB_EVENT_SIZE];
            memcpy(ev, a->payload[a->repeat_pos], UAKB_EVENT_SIZE);
            ev[0] = UAKB_EV_REPEAT;
            emit(a, ev);
            a->repeat_due = now + ms(a->repeat_rate);
            a->last_activity_ms = now;
        }
    }
}

bool app_busy(const app_t *a)
{
    if (a->stable != 0 || a->repeat_pos >= 0) {
        return true;
    }
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        if (a->integ[i]) {
            return true;
        }
    }
    return false;
}

bool app_int_asserted(const app_t *a)
{
    return (a->config & UAKB_CFG_INT_EN) && fifo_count(&a->fifo) > 0;
}

/* ------------------------------------------------------------------ registers */

static uint8_t reg_read(app_t *a, uint8_t addr)
{
    switch (addr) {
    case UAKB_REG_ID:           return UAKB_ID;
    case UAKB_REG_VER_MAJOR:    return UAKB_VERSION_MAJOR;
    case UAKB_REG_VER_MINOR:    return UAKB_VERSION_MINOR;
    case UAKB_REG_STATUS: {
        uint8_t s = state_byte(a);              /* bit0 UA, bit1 Caps, same as event[3] */
        if (a->overflow) {
            s |= UAKB_STATUS_OVERFLOW;          /* cleared in app_i2c_end once the byte was sent */
        }
        return s;
    }
    case UAKB_REG_COUNT:        return fifo_count(&a->fifo);
    case UAKB_REG_CONFIG:       return a->config;
    case UAKB_REG_REPEAT_DELAY: return a->repeat_delay;
    case UAKB_REG_REPEAT_RATE:  return a->repeat_rate;
    case UAKB_REG_LANG:         return a->lang_ua ? 1u : 0u;
    case UAKB_REG_LED:          return a->led_mode;
    case UAKB_REG_BACKLIGHT:    return a->backlight;
    default:                    return 0;
    }
}

static void reg_write(app_t *a, uint8_t addr, uint8_t v)
{
    switch (addr) {
    case UAKB_REG_CONFIG:       a->config = (uint8_t)(v & 0x1Fu); break;
    case UAKB_REG_REPEAT_DELAY: a->repeat_delay = v ? v : 1u; break;
    case UAKB_REG_REPEAT_RATE:  a->repeat_rate = v ? v : 1u; break;
    case UAKB_REG_LANG:         a->lang_ua = v ? 1u : 0u; break;
    case UAKB_REG_LED:          if (v <= UAKB_LED_ON) a->led_mode = v; break;
    case UAKB_REG_BACKLIGHT:    a->backlight = v; break;
    default:                    break;          /* read-only or unused */
    }
}

void app_i2c_write_begin(app_t *a)
{
    a->expect_ptr = true;
    a->cmd_len = 0;
}

void app_i2c_write_byte(app_t *a, uint8_t b)
{
    if (a->expect_ptr) {
        a->ptr = b;
        a->expect_ptr = false;
        return;
    }
    if (a->ptr == UAKB_REG_COMMAND) {
        if (a->cmd_len < sizeof(a->cmd)) {
            a->cmd[a->cmd_len++] = b;
        }
        return;
    }
    reg_write(a, a->ptr, b);
    if (a->ptr != UAKB_REG_FIFO) {
        a->ptr++;
    }
}

void app_i2c_read_begin(app_t *a)
{
    a->rd_ptr = a->ptr;
    a->rd_ev = 0;
    a->rd_byte = 0;
    a->rd_fifo_loaded = 0;
    a->rd_last_was_fifo = false;
    a->rd_snapshot = fifo_count(&a->fifo);
    a->rd_regs_loaded = 0;
    a->rd_status_at = 0;
}

uint8_t app_i2c_read_byte(app_t *a)
{
    if (a->rd_ptr == UAKB_REG_FIFO) {
        uint8_t v = (a->rd_ev < a->rd_snapshot) ? fifo_peek(&a->fifo, a->rd_ev, a->rd_byte) : 0u;
        if (++a->rd_byte >= UAKB_EVENT_SIZE) {
            a->rd_byte = 0;
            a->rd_ev++;
        }
        a->rd_fifo_loaded++;
        a->rd_last_was_fifo = true;
        return v;
    }
    a->rd_last_was_fifo = false;
    if (a->rd_regs_loaded < 255u) {
        a->rd_regs_loaded++;
    }
    if (a->rd_ptr == UAKB_REG_STATUS && a->rd_status_at == 0u) {
        a->rd_status_at = a->rd_regs_loaded;
    }
    return reg_read(a, a->rd_ptr++);
}

void app_i2c_end(app_t *a, bool was_read, uint8_t unsent)
{
    if (was_read) {
        /* STATUS clears the overflow flag only if its byte really left the wire: the byte
         * preloaded for a NACKed transfer is never sent. */
        if (a->rd_status_at != 0u &&
            !(unsent && !a->rd_last_was_fifo && a->rd_status_at == a->rd_regs_loaded)) {
            a->overflow = 0;
        }
        uint16_t sent = a->rd_fifo_loaded;
        if (a->rd_last_was_fifo && unsent && sent) {
            sent--;
        }
        uint8_t pops = (uint8_t)(sent / UAKB_EVENT_SIZE);
        if (pops > a->rd_snapshot) {
            pops = a->rd_snapshot;
        }
        if (pops) {
            fifo_pop(&a->fifo, pops);
        }
        return;
    }
    if (a->ptr == UAKB_REG_COMMAND && a->cmd_len) {
        const bool confirmed = a->cmd_len >= 2 && a->cmd[1] == UAKB_CMD_CONFIRM;
        switch (a->cmd[0]) {
        case UAKB_CMD_CLEAR_FIFO: fifo_clear(&a->fifo); break;
        case UAKB_CMD_RESET:      if (confirmed) a->pending = PENDING_RESET; break;
        case UAKB_CMD_BOOTLOADER: if (confirmed) a->pending = PENDING_BOOTLOADER; break;
        default: break;
        }
        a->cmd_len = 0;
    }
}
