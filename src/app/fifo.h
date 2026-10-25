/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef UAKB_FIFO_H
#define UAKB_FIFO_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

typedef struct {
    uint8_t buf[UAKB_FIFO_DEPTH][UAKB_EVENT_SIZE];
    volatile uint8_t head;      /* next slot to write */
    volatile uint8_t tail;      /* oldest event       */
    volatile uint8_t count;
} fifo_t;

void    fifo_init(fifo_t *f);
bool    fifo_push(fifo_t *f, const uint8_t ev[UAKB_EVENT_SIZE]);   /* false if full */
uint8_t fifo_count(const fifo_t *f);
/* Byte `b` of the i-th oldest event, or 0 if that event does not exist. */
uint8_t fifo_peek(const fifo_t *f, uint8_t i, uint8_t b);
void    fifo_pop(fifo_t *f, uint8_t n);
void    fifo_clear(fifo_t *f);

#endif
