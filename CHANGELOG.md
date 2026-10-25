# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/). The I2C protocol has its own version, reported by the
`VER_MAJOR` / `VER_MINOR` registers; it is still 1.0.

## [Unreleased]

## [1.0.1] - 2026-10-25

First public release.

### Fixed

- I2C: the interrupt handler now services a received byte and STOP before a new address match. Previously, if
  the interrupt was serviced later than a repeated START plus the address byte (about 26 µs at 400 kHz), the
  register pointer byte and the new address were pending together and the read started from the old register.
- `STATUS`: the FIFO overflow flag is now cleared only by a read that actually returns the `STATUS` byte. A
  one-byte read of register `0x02` prefetched `STATUS` without sending it and cleared the flag.

### Added

- Regression scenarios for both fixes in the full-system emulation (`isr:40000`, `status_preload`).
- The firmware version is printed in the NUCLEO start-up log.

## 1.0.0 - 2026-10-25 (internal, not published)

Initial firmware: 5 × 12 matrix scan with 5 ms debounce, UA / EN / Fn layouts, I2C protocol 1.0 with a
16-event FIFO and INT line, auto-repeat, Caps Lock, UA LED, backlight PWM, Stop mode with key and I2C
wake-up, jump to the ST system bootloader, NUCLEO-G071RB test build.

[1.0.1]: ../../releases/tag/v1.0.1
