/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * Keyboard core and I2C register file. Hardware independent: the STM32 layer feeds it
 * a raw key bitmap every millisecond and forwards I2C slave events; host tests drive
 * the same functions directly.
 */
#ifndef UAKB_APP_H
#define UAKB_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"
#include "fifo.h"
#include "keymap.h"

#define DEBOUNCE_MS     5u

typedef enum { PENDING_NONE = 0, PENDING_RESET, PENDING_BOOTLOADER } pending_t;

typedef struct {
    /* keyboard state */
    uint8_t  integ[KEY_COUNT];
    uint64_t stable;                    /* debounced: bit = key down */
    uint8_t  payload[KEY_COUNT][UAKB_EVENT_SIZE];
    uint64_t reported;                  /* keys whose press produced an event */
    int16_t  repeat_pos;                /* -1 = none */
    uint32_t repeat_due;
    volatile uint8_t lang_ua;
    volatile uint8_t caps;
    uint8_t  caps_arm;                  /* Fn+Shift held with no other key yet */
    uint32_t last_activity_ms;

    /* registers */
    volatile uint8_t config;
    volatile uint8_t repeat_delay;
    volatile uint8_t repeat_rate;
    volatile uint8_t led_mode;
    volatile uint8_t backlight;
    volatile uint8_t overflow;

    /* I2C transaction state (touched only from the I2C interrupt) */
    uint8_t  ptr;
    bool     expect_ptr;
    uint8_t  rd_ptr;
    uint8_t  rd_ev, rd_byte;
    uint16_t rd_fifo_loaded;
    bool     rd_last_was_fifo;
    uint8_t  rd_snapshot;               /* events available when the read started */
    uint8_t  rd_regs_loaded;            /* register bytes handed to the I2C peripheral */
    uint8_t  rd_status_at;              /* 1-based position of STATUS among them, 0 = none */
    uint8_t  cmd[4];
    uint8_t  cmd_len;
    volatile pending_t pending;

    fifo_t   fifo;
} app_t;

/* Called for every event that entered the FIFO (weak, default does nothing). */
void app_event_hook(const uint8_t ev[UAKB_EVENT_SIZE]);

/* Critical section hooks, provided by the platform (no-ops in host tests). */
void app_lock(void);
void app_unlock(void);

void app_init(app_t *a);

/* Called every millisecond with the raw (un-debounced) matrix state. */
void app_tick(app_t *a, uint64_t raw, uint32_t now_ms);

/* True if any key is held, a repeat is running, or a debounce is in progress. */
bool app_busy(const app_t *a);

/* INT pin: true = drive low (events waiting). */
bool app_int_asserted(const app_t *a);

/* I2C slave callbacks */
void    app_i2c_write_begin(app_t *a);
void    app_i2c_write_byte(app_t *a, uint8_t b);
void    app_i2c_read_begin(app_t *a);
uint8_t app_i2c_read_byte(app_t *a);
/* unsent = loaded bytes that never left the wire (0 or 1) */
void    app_i2c_end(app_t *a, bool was_read, uint8_t unsent);

#endif
