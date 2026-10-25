/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/* Stand-in for the ST system bootloader ROM at 0x1FFF0000 (the real ROM cannot be emulated).
 * It records that it was entered: marker, its stack pointer and SYSCFG_CFGR1 (MEM_MODE). */
    .syntax unified
    .cpu cortex-m0plus
    .thumb
    .section .vectors, "a"
    .word 0x20001E00            /* initial SP of the "ROM" */
    .word rom_entry + 1
    .text
    .thumb_func
rom_entry:
    ldr  r0, =0x20001F00
    ldr  r1, =0xB007B007
    str  r1, [r0]
    mov  r1, sp
    str  r1, [r0, #4]
    ldr  r2, =0x40010000
    ldr  r1, [r2]
    str  r1, [r0, #8]
1:  b    1b
    .ltorg
