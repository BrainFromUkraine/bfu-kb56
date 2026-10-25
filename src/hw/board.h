/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * Pin map per target. Pins and alternate-function numbers were checked against
 * ST's STM32_open_pin_data (GPIO-STM32G03x / G07x modes) and the BFU-KB56 PCB.
 */
#ifndef UAKB_BOARD_H
#define UAKB_BOARD_H

#include "stm32g0xx.h"

typedef struct { GPIO_TypeDef *port; uint8_t pin; } pin_t;

#if defined(BOARD_BFU_KB56)
/* ---------------------------------------------------------------- BFU-KB56, STM32G030K6T6 */
#define BOARD_NAME          "BFU-KB56 rev F"
#define HAS_MATRIX          1
#define HAS_DEMO            0
#define HAS_UART_LOG        0
#define HAS_BACKLIGHT       1

/* Rows ROW0..ROW4 = PB0..PB4, active low */
#define ROW_PORT            GPIOB
#define ROW_FIRST_PIN       0u
/* Columns COL0..COL11, input pull-up; EXTI line numbers are all different */
#define COLUMN_PINS { \
    { GPIOA, 0 }, { GPIOA, 1 }, { GPIOA, 2 }, { GPIOA, 3 }, { GPIOA, 4 }, { GPIOA, 5 }, \
    { GPIOA, 6 }, { GPIOA, 7 }, { GPIOA, 8 }, { GPIOB, 9 }, { GPIOA, 11 }, { GPIOA, 12 } }

#define I2C_SCL             { GPIOB, 6 }    /* AF6 I2C1_SCL */
#define I2C_SDA             { GPIOB, 7 }    /* AF6 I2C1_SDA */
#define I2C_AF              6u
#define INT_PIN             { GPIOB, 5 }    /* open drain, active low */
#define LED_UA_PIN          { GPIOC, 6 }    /* active high */
#define BL_PWM_PIN          { GPIOB, 8 }    /* AF2 TIM16_CH1 */
#define BL_PWM_AF           2u
#define ADDR_SEL_PIN        { GPIOA, 15 }   /* JP3 closed = low = alternate address */

#elif defined(BOARD_NUCLEO_G071RB)
/* ---------------------------------------------------------------- NUCLEO-G071RB protocol test build */
#define BOARD_NAME          "NUCLEO-G071RB test"
#define HAS_MATRIX          0
#define HAS_DEMO            1
#define HAS_UART_LOG        1
#define HAS_BACKLIGHT       0

#define I2C_SCL             { GPIOB, 8 }    /* AF6 I2C1_SCL, Arduino D15 */
#define I2C_SDA             { GPIOB, 9 }    /* AF6 I2C1_SDA, Arduino D14 */
#define I2C_AF              6u
#define INT_PIN             { GPIOA, 10 }   /* open drain, active low */
#define LED_UA_PIN          { GPIOA, 5 }    /* LD4 */
#define BUTTON_PIN          { GPIOC, 13 }   /* B1 USER */
#define UART_TX_PIN         { GPIOA, 2 }    /* AF1 USART2_TX -> ST-LINK virtual COM */
#define UART_AF             1u

#else
#error "Define BOARD_BFU_KB56 or BOARD_NUCLEO_G071RB"
#endif

#define SYSCLK_HZ           16000000u       /* HSI16, also I2C kernel clock */

#endif
