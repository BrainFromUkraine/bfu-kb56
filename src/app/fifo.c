/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "fifo.h"

void fifo_init(fifo_t *f)
{
    memset(f, 0, sizeof(*f));
}

bool fifo_push(fifo_t *f, const uint8_t ev[UAKB_EVENT_SIZE])
{
    if (f->count >= UAKB_FIFO_DEPTH) {
        return false;
    }
    memcpy(f->buf[f->head], ev, UAKB_EVENT_SIZE);
    f->head = (uint8_t)((f->head + 1u) % UAKB_FIFO_DEPTH);
    f->count++;
    return true;
}

uint8_t fifo_count(const fifo_t *f)
{
    return f->count;
}

uint8_t fifo_peek(const fifo_t *f, uint8_t i, uint8_t b)
{
    if (i >= f->count || b >= UAKB_EVENT_SIZE) {
        return 0;
    }
    return f->buf[(f->tail + i) % UAKB_FIFO_DEPTH][b];
}

void fifo_pop(fifo_t *f, uint8_t n)
{
    if (n > f->count) {
        n = f->count;
    }
    f->tail = (uint8_t)((f->tail + n) % UAKB_FIFO_DEPTH);
    f->count = (uint8_t)(f->count - n);
}

void fifo_clear(fifo_t *f)
{
    f->head = f->tail = f->count = 0;
}
