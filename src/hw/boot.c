/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * Entering the ST system bootloader (ROM, 0x1FFF0000 on STM32G0).
 * A magic word in a RAM section that startup code never clears survives NVIC_SystemReset();
 * right after reset, before any peripheral is touched, we map system memory at 0x0 and
 * jump to the bootloader. It then accepts firmware over I2C1 (PB6/PB7) or USART.
 */
#include "hw.h"

#define BOOT_MAGIC          0xB0071D4Du
#define SYSMEM_BASE         0x1FFF0000u

__attribute__((section(".noinit"))) static volatile uint32_t boot_magic;

void boot_enter_system_bootloader(void)
{
    boot_magic = BOOT_MAGIC;
    __DSB();
    NVIC_SystemReset();
}

void boot_early_check(void)
{
    if (boot_magic != BOOT_MAGIC) {
        return;
    }
    boot_magic = 0;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | SYSCFG_CFGR1_MEM_MODE_0;   /* system flash at 0x0 */

    const uint32_t sp = *(volatile const uint32_t *)SYSMEM_BASE;
    const uint32_t pc = *(volatile const uint32_t *)(SYSMEM_BASE + 4u);
    __set_MSP(sp);
    ((void (*)(void))pc)();
    for (;;) {
    }
}
