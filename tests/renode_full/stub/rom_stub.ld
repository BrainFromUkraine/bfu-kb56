/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
SECTIONS { . = 0x1FFF0000; .vectors : { KEEP(*(.vectors)) } .text : { *(.text*) } }
