<!-- SPDX-FileCopyrightText: 2026 BFU Electronics LTD -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# BFU-KB56 Hardware Guide

Everything you need to connect, configure and flash the BFU-KB56 keyboard module.

> The board design files (schematic, PCB layout, BOM) are not part of this repository.
> This guide documents the board's external interface only.

## At a glance

| | |
|---|---|
| Keys | 56, in a 5 × 12 matrix with a diode per key (full n-key rollover) |
| Layouts | Ukrainian (ЙЦУКЕН with ґ, є, і, ї, ʼ, ₴) and English (QWERTY), plus an Fn layer |
| Microcontroller | STM32G030K6T6 (Cortex-M0+, 32 KB flash, 8 KB RAM) |
| Interface | I2C slave, 100/400 kHz, address 0x5E (0x5D with JP3 bridged), active-low INT line |
| Supply | 3.3–5 V via J1 (on-board 3.3 V LDO) |
| Logic levels | 3.3 V; SDA, SCL and INT are open-drain and 5 V tolerant |
| Indicator | UA layout LED |
| Board size | 100 × 50 mm, four M2 mounting holes, 3.5 mm from each edge (93 × 43 mm pitch) |

## Connector J1 — host

JST SH, 1.0 mm pitch, 5 pins, side entry (mating cable: JST SHR-05V-S housing, or any ready-made SH 5-pin
cable). J1 sits on the back of the board, centred on the top edge.

| Pin | Signal | Notes |
|---|---|---|
| 1 | GND | |
| 2 | VCC | 3.3–5 V |
| 3 | SDA | I2C data |
| 4 | SCL | I2C clock |
| 5 | INT | Active low, open-drain. Low while events are waiting |

## Solder jumpers (back side)

| Jumper | Default | Function |
|---|---|---|
| JP1, JP2 | **Bridged** (copper trace) | Connect the on-board 4.7 kΩ pull-ups on SDA and SCL. Cut the thin trace between the pads to disconnect them, e.g. when several devices share the bus and the total pull-up would be too strong. Re-bridge with a blob of solder |
| JP3 | Open | I2C address. Open = **0x5E**, bridged = **0x5D**. Read once at power-up |

A host with its own pull-ups to 5 V is safe: they cannot push current back into the keyboard's 3.3 V supply.

## Wiring examples

### ESP32 (3.3 V)

| Keyboard J1 | ESP32 |
|---|---|
| 1 GND | GND |
| 2 VCC | 3V3 |
| 3 SDA | GPIO 21 (or any) |
| 4 SCL | GPIO 22 (or any) |
| 5 INT | Any input GPIO |

No external pull-ups are needed with JP1/JP2 bridged. Cable lengths up to 30 cm work at 400 kHz;
use 100 kHz for longer cables.

### 5 V host (Arduino Uno, etc.)

Power the keyboard from 5 V. The on-board pull-ups only reach 3.3 V, which is below the input-high level of a
5 V AVR's I2C pins (0.7 × VCC = 3.5 V), so add 4.7 kΩ pull-ups to 5 V on SDA and SCL at the host.
JP1/JP2 can stay bridged; the 5 V pull-ups do not back-feed the keyboard. All keyboard I/O on J1 is
5 V tolerant. The AVR `Wire` buffer is 32 bytes: read at most 4 events per transaction.

### Several devices on one bus

Make sure only one set of pull-ups is active (roughly 2.2–10 kΩ total), and give each keyboard a unique address
with JP3 (two keyboards maximum per bus).

## SWD pads J2 — programming

Five 2.54 mm pads on the back, for a pogo-pin clip or test probes:

| J2 pad | Signal |
|---|---|
| 1 | 3V3 |
| 2 | SWDIO |
| 3 | SWCLK |
| 4 | NRST |
| 5 | GND |

### Using a NUCLEO board as the programmer

Any ST-LINK works. The on-board ST-LINK/V2-1 of a **NUCLEO-G071RB** is convenient:

1. Remove **both CN2 jumpers** on the NUCLEO. This disconnects the ST-LINK from its own STM32 so it acts as an
   external programmer.
2. Connect NUCLEO **CN4** (SWD) to the keyboard's **J2**:

   | NUCLEO CN4 | Keyboard J2 |
   |---|---|
   | 1 VDD_TARGET | 1 3V3 |
   | 2 SWCLK | 3 SWCLK |
   | 3 GND | 5 GND |
   | 4 SWDIO | 2 SWDIO |
   | 5 NRST | 4 NRST |
   | 6 SWO | — |

3. Power the keyboard through J1 (3.3–5 V), or connect the NUCLEO's **3V3** pin (Arduino power header) to pad J2-1.
   CN4 VDD_TARGET only senses the target voltage; it does not power the board.
4. Flash as described in the [README](../README.md#flashing).

Both OpenOCD and STM32CubeProgrammer connect **under reset** (NRST), so the firmware's sleep mode does not get in
the way. If NRST is not connected, power the keyboard while holding any key and start flashing immediately.

## Firmware update over I2C

Writing `[0x20, 0xB0, 0xA5]` restarts the keyboard into the STM32 ROM bootloader, which accepts new firmware over
I2C (ST AN4221 protocol). The bootloader lives in ROM and cannot be erased, so the keyboard cannot be bricked: in
the worst case it can always be re-flashed over SWD. See [PROTOCOL.md](PROTOCOL.md#bootloader).

## Power consumption

Design estimates at 3.3 V:

| State | Current |
|---|---|
| Sleep (Stop 1), UA LED off | ≈ 12 µA |
| Sleep, UA LED on | ≈ 0.56 mA |

The LED can be switched off from the host (`LED` register, `0x09`) for battery-powered devices.
