/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef UAKB_KEYMAP_H
#define UAKB_KEYMAP_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

/* A key action:
 *   0                    no key at this matrix position
 *   1 .. 0x10FFFF        Unicode text (control characters included)
 *   KA_SPECIAL(code)     non-text key, code = UAKB_SP_*
 *   KA_MOD(bit)          modifier key, bit = UAKB_MOD_*
 *   KA_APOS              the UA apostrophe (U+02BC or ASCII, depends on CONFIG) */
typedef uint32_t kact_t;

#define KA_NONE             0u
#define KA_SPECIAL_FLAG     0x80000000u
#define KA_MOD_FLAG         0x40000000u
#define KA_APOS             0x20000000u
#define KA_SPECIAL(code)    (KA_SPECIAL_FLAG | (uint32_t)(code))
#define KA_MOD(bit)         (KA_MOD_FLAG | (uint32_t)(bit))

#define KA_IS_TEXT(a)       ((a) != 0u && (a) <= 0x10FFFFu)
#define KA_IS_SPECIAL(a)    (((a) & KA_SPECIAL_FLAG) != 0u)
#define KA_IS_MOD(a)        (((a) & KA_MOD_FLAG) != 0u)

#define KEY_COUNT           (UAKB_ROWS * UAKB_COLS)

/* Physical positions used by the keyboard core */
#define POS_SHIFT           UAKB_KEYCODE(3, 0)
#define POS_FN              UAKB_KEYCODE(4, 0)
#define POS_CTRL            UAKB_KEYCODE(4, 1)
#define POS_ALT             UAKB_KEYCODE(4, 2)

/* Resolve what a key does for the given state. */
kact_t keymap_resolve(uint8_t pos, bool lang_ua, bool shift, bool fn, bool caps, bool apos_ascii);

/* True if pos is a populated key position. */
bool keymap_present(uint8_t pos);

#endif
