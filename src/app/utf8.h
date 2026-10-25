/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef UAKB_UTF8_H
#define UAKB_UTF8_H

#include <stdint.h>

/* Encode one Unicode code point into out[0..3]; unused bytes are zeroed.
 * Returns the number of bytes written (1..4), or 0 for an invalid code point. */
uint8_t utf8_encode(uint32_t cp, uint8_t out[4]);

#endif
