<!-- SPDX-FileCopyrightText: 2026 BFU Electronics LTD -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# BFU-KB56 I2C Protocol, version 1.0

This document specifies how a host (ESP32, Raspberry Pi, any I2C master) talks to the BFU-KB56 keyboard.
The constants used by the firmware live in [`src/app/protocol.h`](../src/app/protocol.h); this file is their
normative description.

> This specification is licensed under [CC BY 4.0](../LICENSES/CC-BY-4.0.txt). You may implement it in your
> own host libraries and products, commercial or not, as long as you credit BFU Electronics LTD.

## Contents

1. [Physical layer](#1-physical-layer)
2. [Transactions](#2-transactions)
3. [Register map](#3-register-map)
4. [Event format](#4-event-format)
5. [FIFO and the INT line](#5-fifo-and-the-int-line)
6. [Commands](#6-commands-register-0x20)
7. [Power management](#7-power-management)
8. [Key map](#8-key-map)

## 1. Physical layer

| Parameter | Value |
|---|---|
| Bus | I2C Standard mode (100 kHz) and Fast mode (400 kHz) |
| Address (7-bit) | **0x5E** with JP3 open (default), **0x5D** with JP3 bridged. Sampled once at power-up |
| Clock stretching | Yes, short: a few µs per byte, up to ~60 µs on the first byte after a wake-up from Stop. The host must support it |
| SDA/SCL pull-ups | 4.7 kΩ to 3.3 V on the board, connected by default through solder jumpers JP1/JP2 (cut to disconnect). Host pull-ups to 5 V are safe: they do not back-feed the keyboard's supply |
| INT line | Open-drain, **active low**, 100 kΩ pull-up on the board. A host pull-up (`INPUT_PULLUP`) is harmless |
| Connector J1 (JST SH, 1.0 mm) | 1 GND, 2 VCC, 3 SDA, 4 SCL, 5 INT |
| Supply | 3.3–5 V from the host (on-board 3.3 V LDO) |

See [HARDWARE.md](HARDWARE.md) for wiring examples.

## 2. Transactions

**Write:** `START, addr+W, REG, data0, data1, …, STOP`.
The first byte after the address is the register pointer. The following bytes go to `REG`, `REG+1`, …
(auto-increment). Writes to read-only registers are ignored, but the pointer still advances.

**Read:** `START, addr+W, REG, RESTART, addr+R, bytes…, NACK, STOP`.
A read always starts at the register set by the last write and auto-increments.
Reading does not change the stored pointer: a second read without a new write starts at the same `REG` again.
After reset the pointer is `0x00`.

Two registers are exceptions: on `FIFO` (`0x10`) the pointer does not advance and every byte read comes from
the event queue; on `COMMAND` (`0x20`) the pointer does not advance and the written bytes form one command.

## 3. Register map

| Addr | Name | Access | Reset | Description |
|---|---|---|---|---|
| 0x00 | `ID` | R | 0x4B | Device identifier (`'K'`) |
| 0x01 | `VER_MAJOR` | R | 1 | Protocol version, major |
| 0x02 | `VER_MINOR` | R | 0 | Protocol version, minor |
| 0x03 | `STATUS` | R | 0x01 | bit 0: UA layout active, bit 1: Caps Lock, bit 7: **FIFO overflow** (cleared by a read that returns `STATUS`) |
| 0x04 | `COUNT` | R | 0 | Number of events in the FIFO (0…16) |
| 0x05 | `CONFIG` | R/W | 0x0D | See bits below. Only bits 0–4 are writable |
| 0x06 | `REPEAT_DELAY` | R/W | 50 | Auto-repeat delay, ×10 ms (50 = 500 ms). Writing 0 stores 1 |
| 0x07 | `REPEAT_RATE` | R/W | 5 | Auto-repeat period, ×10 ms (5 = 50 ms = 20 chars/s). Writing 0 stores 1 |
| 0x08 | `LANG` | R/W | 1 | 0 = EN, 1 = UA. A write switches the layout **without** generating an event |
| 0x09 | `LED` | R/W | 0 | UA indicator LED: 0 = auto (on while UA is active), 1 = off, 2 = on. Other values are ignored |
| 0x0A | `BACKLIGHT` | R/W | 0 | Backlight PWM, 0…255 (optional, not fitted on standard boards) |
| 0x10 | `FIFO` | R | — | Event queue, 8 bytes per event |
| 0x20 | `COMMAND` | W | — | Commands, see [section 6](#6-commands-register-0x20) |

Any other address reads as 0; writes to it are ignored.

### `CONFIG` bits

| Bit | Mask | Name | Default | Meaning |
|---|---|---|---|---|
| 0 | 0x01 | `INT_EN` | 1 | Hold INT low while the FIFO is not empty |
| 1 | 0x02 | `RELEASES` | 0 | Also report key releases |
| 2 | 0x04 | `REPEAT` | 1 | Auto-repeat a held key |
| 3 | 0x08 | `SLEEP` | 1 | Allow Stop mode when idle |
| 4 | 0x10 | `APOS_ASCII` | 0 | Send the UA apostrophe as ASCII `'` (U+0027) instead of `ʼ` (U+02BC) |

## 4. Event format

Each event is 8 bytes:

| Byte | Field | Meaning |
|---|---|---|
| 0 | `type` | 1 = press, 2 = release, 3 = auto-repeat, 0 = empty (FIFO exhausted) |
| 1 | `key` | Matrix position: `row × 12 + column` (0…59) |
| 2 | `mods` | Modifiers held: bit 0 Shift, bit 1 Ctrl, bit 2 Alt, bit 3 Fn |
| 3 | `state` | bit 0: UA layout, bit 1: Caps Lock (state **after** the event) |
| 4–7 | `text` | The character in UTF-8, zero-padded (1–4 bytes) |

If `byte4 == 0`, the key produces no text and `byte5` holds a **special code**:

| Code | Key | Code | Key |
|---|---|---|---|
| 0x01 | ↑ Up | 0x05 | Home |
| 0x02 | ↓ Down | 0x06 | End |
| 0x03 | ← Left | 0x07 | Page Up |
| 0x04 | → Right | 0x08 | Page Down |
| 0x10…0x1B | F1…F12 | 0x20 | Layout switched (new layout in `state`) |
| 0x21 | Caps Lock toggled (new state in `state`) | 0x22 | Backlight step (new value in `BACKLIGHT`) |

Control keys produce ASCII text: Enter `0x0A`, Backspace `0x08`, Tab `0x09`, Esc `0x1B`, Delete `0x7F`,
Space `0x20`.

Examples:

```
01 0D 00 01  D1 86 00 00   press "ц" (key 13), UA layout
01 29 01 00  42 00 00 00   Shift + "B" (key 41), EN layout
01 33 00 00  00 20 00 00   layout key: now EN
03 0B 00 01  08 00 00 00   auto-repeat of Backspace
01 39 02 01  00 03 00 00   Ctrl + ← (key 57), special code 0x03
```

### Event rules

- **Modifiers** (Shift, Ctrl, Alt, Fn) do not generate events on their own; their state is reported in the
  `mods` field of every event.
- Text is always final: Shift, Caps Lock and the active layout are already applied. `Ctrl+ц` yields the text
  `ц` with `mods = 0x02`; the host interprets shortcuts.
- **Caps Lock** is Fn + Shift: hold Fn, press and release Shift without touching any other key. The toggle
  happens on the Shift release (event 0x21). Caps Lock affects letters only.
- **Layout** is switched by the 🌐 key (key 51), event 0x20. In auto mode the UA LED shows the active layout.
- **Auto-repeat** applies only to the most recently pressed key that produces text, arrows and F-keys. It
  generates type 3 events with the same content as the press. The layout, Caps and backlight keys do not repeat.
- **Releases** (when `RELEASES = 1`) carry the same text as the press, even if Shift was released in between,
  plus the current `mods`.
- Debounce is 5 ms. Modifiers are processed before other keys in the same 1 ms tick, so pressing Shift and a
  letter simultaneously yields a capital letter.

## 5. FIFO and the INT line

- Depth is **16 events**. When the queue is full, new events are dropped and bit 7 of `STATUS` is set.
- INT is low while at least one event is queued (and `INT_EN = 1`). The host can wait for the falling edge of
  INT instead of polling the bus.
- Reading from `0x10` returns events back to back: 8 bytes of the first, 8 bytes of the second, and so on.
  After the last queued event the keyboard returns zeros (`type = 0`).
- An event is **removed from the queue only after the host has read all 8 of its bytes**. If a transaction is
  cut in the middle of an event, the next read returns that event again from its first byte: nothing is lost
  and nothing is duplicated.
- The number of events visible to a transaction is fixed when it starts. Events created during the read stay
  queued for the next one.

Recommended host loop:

```c
// 1) INT is LOW (or every 10–20 ms if INT is not connected)
uint8_t n = read_reg(0x04);                 // COUNT
if (n) {
    uint8_t buf[16 * 8];
    write(addr, (uint8_t[]){0x10}, 1);      // pointer to FIFO
    read(addr, buf, n * 8);                 // n events in one transaction
    for (int i = 0; i < n; i++) handle(&buf[i * 8]);
}
```

Arduino (`Wire`):

```cpp
Wire.beginTransmission(0x5E); Wire.write(0x10); Wire.endTransmission(false);
uint8_t got = Wire.requestFrom((uint8_t)0x5E, (uint8_t)(n * 8));   // got == n * 8 on success
```

The ESP32 `Wire` buffer is 128 bytes, exactly 16 events. On AVR boards (Uno, Nano) it is 32 bytes, so read at
most 4 events per transaction and repeat until `COUNT` is 0.

## 6. Commands (register 0x20)

Write `[0x20, CMD]` or `[0x20, CMD, 0xA5]`. Destructive commands run only with the confirmation byte `0xA5`
and only after STOP.

| CMD | Confirmation | Action |
|---|---|---|
| 0x01 | no | Clear the FIFO |
| 0x52 | `0xA5` | Software reset (all settings return to their defaults) |
| 0xB0 | `0xA5` | Jump to the **ST system bootloader** (firmware update over I2C) |

Settings are not stored in flash: after power-up or reset the host sets them again.

### Bootloader

After `[0x20, 0xB0, 0xA5]` the keyboard restarts into the STM32G030 ROM bootloader (I2C1 on the same
PB6/PB7 pins). The bootloader answers at a **different address** than the keyboard and speaks the ST AN4221
protocol, not this one. The exact address for STM32G03x is listed in ST AN2606. To leave the bootloader without
flashing, cycle power or issue the Go command (0x21) to `0x08000000`.

## 7. Power management

When `SLEEP = 1`, no key is held, `BACKLIGHT = 0` and there has been no activity for 100 ms, the
microcontroller enters Stop 1. It wakes up on any key press (EXTI on the columns) or when the host addresses the
keyboard (I2C address-match wake-up). The first transaction after a wake-up works normally: the wake-up happens
during clock stretching. INT and the UA LED keep their state in Stop. Unread events in the queue do not prevent
sleep.

## 8. Key map

`key = row × 12 + column`. Base layer, UA / EN:

| row\col | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | ʼ / \` | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 | ⌫ |
| 1 | й / q | ц / w | у / e | к / r | е / t | н / y | г / u | ш / i | щ / o | з / p | х / [ | ї / ] |
| 2 | ф / a | і / s | в / d | а / f | п / g | р / h | о / j | л / k | д / l | ж / ; | є / ' | ⏎ |
| 3 | Shift | я / z | ч / x | с / c | м / v | и / b | т / n | ь / m | б / , | ю / . | . / / | ↑ |
| 4 | Fn | Ctrl | Alt | 🌐 | — | Space | — | — | — | ← | ↓ | → |

Shift (UA): `₴ ! " № ; % : ? * ( )` over `ʼ 1…0`, `,` over `.`.
Shift (EN): `~ ! @ # $ % ^ & * ( )`, `{ } : " < > ?`.

Fn layer (identical for UA and EN):

| row\col | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Esc | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 | F9 | F10 | Del |
| 1 | Tab | - | _ | = | + | ~ | ґ (Shift: Ґ) | [ | ] | { | } | \\ (Shift: \|) |
| 2 | ! | @ | # | $ | % | ^ | & | * | ( | ) | " | F11 |
| 3 | Caps | \` | \| | / | ? | : | ; | ' | < | > | , | PgUp |
| 4 | Fn | F12 | Alt | Light | — | Space | — | — | — | Home | PgDn | End |

Matrix positions 52, 54, 55 and 56 are not populated.
