/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * BFU-KB56 — I2C protocol definitions.
 * Shared by the firmware, the host-side tests and (later) the host library.
 * The authoritative description is docs/PROTOCOL.md.
 */
#ifndef UAKB_PROTOCOL_H
#define UAKB_PROTOCOL_H

#include <stdint.h>

/* 7-bit I2C addresses (JP3 open / JP3 closed) */
#define UAKB_I2C_ADDR_DEFAULT   0x5Eu
#define UAKB_I2C_ADDR_ALT       0x5Du

/* Register map */
#define UAKB_REG_ID             0x00u   /* R   constant UAKB_ID                       */
#define UAKB_REG_VER_MAJOR      0x01u   /* R                                         */
#define UAKB_REG_VER_MINOR      0x02u   /* R                                         */
#define UAKB_REG_STATUS         0x03u   /* R   see UAKB_STATUS_*; OVF cleared on read */
#define UAKB_REG_COUNT          0x04u   /* R   events waiting in the FIFO             */
#define UAKB_REG_CONFIG         0x05u   /* R/W see UAKB_CFG_*                         */
#define UAKB_REG_REPEAT_DELAY   0x06u   /* R/W autorepeat delay,  x10 ms              */
#define UAKB_REG_REPEAT_RATE    0x07u   /* R/W autorepeat period, x10 ms              */
#define UAKB_REG_LANG           0x08u   /* R/W 0 = EN, 1 = UA                         */
#define UAKB_REG_LED            0x09u   /* R/W UAKB_LED_*                             */
#define UAKB_REG_BACKLIGHT      0x0Au   /* R/W PWM duty 0..255                        */
#define UAKB_REG_FIFO           0x10u   /* R   8-byte events, read pops whole events  */
#define UAKB_REG_COMMAND        0x20u   /* W   UAKB_CMD_*                             */

#define UAKB_ID                 0x4Bu   /* 'K' */
#define UAKB_VERSION_MAJOR      1u
#define UAKB_VERSION_MINOR      0u

/* Firmware release (VER_MAJOR/VER_MINOR above are the protocol version) */
#define FW_VERSION              "1.0.1"

/* STATUS bits */
#define UAKB_STATUS_LANG_UA     0x01u
#define UAKB_STATUS_CAPS        0x02u
#define UAKB_STATUS_OVERFLOW    0x80u

/* CONFIG bits */
#define UAKB_CFG_INT_EN         0x01u   /* drive INT low while the FIFO is not empty  */
#define UAKB_CFG_RELEASES       0x02u   /* also report key releases                   */
#define UAKB_CFG_REPEAT         0x04u   /* autorepeat enabled                         */
#define UAKB_CFG_SLEEP          0x08u   /* allow Stop mode when idle                  */
#define UAKB_CFG_APOS_ASCII     0x10u   /* UA apostrophe as ASCII ' instead of U+02BC */
#define UAKB_CFG_DEFAULT        (UAKB_CFG_INT_EN | UAKB_CFG_REPEAT | UAKB_CFG_SLEEP)

#define UAKB_REPEAT_DELAY_DEFAULT  50u  /* 500 ms */
#define UAKB_REPEAT_RATE_DEFAULT    5u  /*  50 ms */

/* LED modes */
#define UAKB_LED_AUTO           0u      /* on while the language is UA */
#define UAKB_LED_OFF            1u
#define UAKB_LED_ON             2u

/* COMMAND codes. Destructive commands need UAKB_CMD_CONFIRM as the next byte. */
#define UAKB_CMD_CLEAR_FIFO     0x01u
#define UAKB_CMD_RESET          0x52u   /* + confirm */
#define UAKB_CMD_BOOTLOADER     0xB0u   /* + confirm: jump to the ST system bootloader */
#define UAKB_CMD_CONFIRM        0xA5u

/* ---------------------------------------------------------------- events */
#define UAKB_EVENT_SIZE         8u
#define UAKB_FIFO_DEPTH         16u

/* event[0]: type */
#define UAKB_EV_NONE            0u
#define UAKB_EV_PRESS           1u
#define UAKB_EV_RELEASE         2u
#define UAKB_EV_REPEAT          3u

/* event[1]: key code = matrix position row * 12 + col (0..59) */
#define UAKB_ROWS               5u
#define UAKB_COLS               12u
#define UAKB_KEYCODE(r, c)      ((uint8_t)((r) * UAKB_COLS + (c)))

/* event[2]: modifiers held at the time of the event */
#define UAKB_MOD_SHIFT          0x01u
#define UAKB_MOD_CTRL           0x02u
#define UAKB_MOD_ALT            0x04u
#define UAKB_MOD_FN             0x08u

/* event[3]: keyboard state */
#define UAKB_STATE_LANG_UA      0x01u
#define UAKB_STATE_CAPS         0x02u

/* event[4..7]: UTF-8 text, zero padded.
 * If event[4] == 0 the key produced no text and event[5] holds a special code. */
#define UAKB_SP_NONE            0x00u
#define UAKB_SP_UP              0x01u
#define UAKB_SP_DOWN            0x02u
#define UAKB_SP_LEFT            0x03u
#define UAKB_SP_RIGHT           0x04u
#define UAKB_SP_HOME            0x05u
#define UAKB_SP_END             0x06u
#define UAKB_SP_PGUP            0x07u
#define UAKB_SP_PGDN            0x08u
#define UAKB_SP_F1              0x10u   /* F1..F12 = 0x10..0x1B */
#define UAKB_SP_LANG            0x20u   /* language toggled; see event[3] */
#define UAKB_SP_CAPS            0x21u   /* Caps Lock toggled; see event[3] */
#define UAKB_SP_LIGHT           0x22u   /* backlight step; see BACKLIGHT register */

/* Control keys produce ASCII control characters as text:
 * Enter 0x0A, Backspace 0x08, Tab 0x09, Esc 0x1B, Delete 0x7F, Space 0x20. */

#endif
