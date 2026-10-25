/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef UAKB_HW_H
#define UAKB_HW_H

#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "app.h"

extern app_t g_app;
extern volatile uint32_t g_ms;

/* matrix.c */
void     matrix_init(void);
uint64_t matrix_read(void);
void     matrix_prepare_sleep(void);        /* all rows low, column EXTI armed   */
bool     matrix_any_column_low(void);
void     matrix_after_wake(void);           /* EXTI off, rows back to idle high  */

/* i2c_slave.c */
void     i2c_slave_init(uint8_t addr7);
bool     i2c_slave_busy(void);
void     int_pin_update(void);

/* led.c */
void     led_init(void);
void     led_update(void);

/* power.c */
void     power_init(void);
void     power_idle(uint32_t now);

/* boot.c */
void     boot_early_check(void);
void     boot_enter_system_bootloader(void);

/* uart_log.c (Nucleo only) */
void     log_init(void);
void     log_str(const char *s);

/* demo.c (Nucleo only) */
void     demo_init(void);
uint64_t demo_raw(uint32_t now);

#endif
