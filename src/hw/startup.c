/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimal Cortex-M0+ startup for STM32G0 (32 device interrupts).
 * The bootloader request is checked before .data/.bss are touched, so the jump to the
 * ST system bootloader happens from a clean reset state.
 */
#include <stdint.h>
#include "hw.h"

extern uint32_t _estack;
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[];
int main(void);

void Reset_Handler(void);
void Default_Handler(void);

#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_ALIAS;
void HardFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void) WEAK_ALIAS;
void PendSV_Handler(void) WEAK_ALIAS;
void SysTick_Handler(void) WEAK_ALIAS;
void WWDG_IRQHandler(void) WEAK_ALIAS;
void RTC_TAMP_IRQHandler(void) WEAK_ALIAS;
void FLASH_IRQHandler(void) WEAK_ALIAS;
void RCC_IRQHandler(void) WEAK_ALIAS;
void EXTI0_1_IRQHandler(void) WEAK_ALIAS;
void EXTI2_3_IRQHandler(void) WEAK_ALIAS;
void EXTI4_15_IRQHandler(void) WEAK_ALIAS;
void DMA1_Channel1_IRQHandler(void) WEAK_ALIAS;
void DMA1_Channel2_3_IRQHandler(void) WEAK_ALIAS;
void DMA1_Ch4_7_DMAMUX1_OVR_IRQHandler(void) WEAK_ALIAS;
void ADC1_IRQHandler(void) WEAK_ALIAS;
void TIM1_BRK_UP_TRG_COM_IRQHandler(void) WEAK_ALIAS;
void TIM1_CC_IRQHandler(void) WEAK_ALIAS;
void TIM2_IRQHandler(void) WEAK_ALIAS;
void TIM3_IRQHandler(void) WEAK_ALIAS;
void TIM6_DAC_LPTIM1_IRQHandler(void) WEAK_ALIAS;
void TIM7_LPTIM2_IRQHandler(void) WEAK_ALIAS;
void TIM14_IRQHandler(void) WEAK_ALIAS;
void TIM15_IRQHandler(void) WEAK_ALIAS;
void TIM16_IRQHandler(void) WEAK_ALIAS;
void TIM17_IRQHandler(void) WEAK_ALIAS;
void I2C1_IRQHandler(void) WEAK_ALIAS;
void I2C2_IRQHandler(void) WEAK_ALIAS;
void SPI1_IRQHandler(void) WEAK_ALIAS;
void SPI2_IRQHandler(void) WEAK_ALIAS;
void USART1_IRQHandler(void) WEAK_ALIAS;
void USART2_IRQHandler(void) WEAK_ALIAS;
void USART3_4_LPUART1_IRQHandler(void) WEAK_ALIAS;
void CEC_IRQHandler(void) WEAK_ALIAS;

__attribute__((section(".isr_vector"), used))
void (*const g_vectors[16 + 32])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0,
    SVC_Handler,
    0, 0,
    PendSV_Handler,
    SysTick_Handler,
    /* device interrupts 0..31 (RM0444 vector table) */
    WWDG_IRQHandler,                    /*  0 */
    0,                                  /*  1 PVD (not on G030/G071 value line use) */
    RTC_TAMP_IRQHandler,                /*  2 */
    FLASH_IRQHandler,                   /*  3 */
    RCC_IRQHandler,                     /*  4 */
    EXTI0_1_IRQHandler,                 /*  5 */
    EXTI2_3_IRQHandler,                 /*  6 */
    EXTI4_15_IRQHandler,                /*  7 */
    0,                                  /*  8 UCPD/USB */
    DMA1_Channel1_IRQHandler,           /*  9 */
    DMA1_Channel2_3_IRQHandler,         /* 10 */
    DMA1_Ch4_7_DMAMUX1_OVR_IRQHandler,  /* 11 */
    ADC1_IRQHandler,                    /* 12 */
    TIM1_BRK_UP_TRG_COM_IRQHandler,     /* 13 */
    TIM1_CC_IRQHandler,                 /* 14 */
    TIM2_IRQHandler,                    /* 15 */
    TIM3_IRQHandler,                    /* 16 */
    TIM6_DAC_LPTIM1_IRQHandler,         /* 17 */
    TIM7_LPTIM2_IRQHandler,             /* 18 */
    TIM14_IRQHandler,                   /* 19 */
    TIM15_IRQHandler,                   /* 20 */
    TIM16_IRQHandler,                   /* 21 */
    TIM17_IRQHandler,                   /* 22 */
    I2C1_IRQHandler,                    /* 23 */
    I2C2_IRQHandler,                    /* 24 */
    SPI1_IRQHandler,                    /* 25 */
    SPI2_IRQHandler,                    /* 26 */
    USART1_IRQHandler,                  /* 27 */
    USART2_IRQHandler,                  /* 28 */
    USART3_4_LPUART1_IRQHandler,        /* 29 */
    CEC_IRQHandler,                     /* 30 */
    0,                                  /* 31 AES/RNG */
};

void Reset_Handler(void)
{
    boot_early_check();

    /* linker-provided bounds, compared as addresses */
    const uintptr_t data_words = ((uintptr_t)_edata - (uintptr_t)_sdata) / 4u;
    for (uintptr_t i = 0; i < data_words; i++) {
        _sdata[i] = _sidata[i];
    }
    const uintptr_t bss_words = ((uintptr_t)_ebss - (uintptr_t)_sbss) / 4u;
    for (uintptr_t i = 0; i < bss_words; i++) {
        _sbss[i] = 0;
    }
    (void)main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}
