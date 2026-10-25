# Contributing to BFU-KB56

Thank you for your interest in the project. Bug reports, protocol questions, host-side drivers and firmware
improvements are all welcome.

## Reporting bugs

Please open an issue and include:

- the firmware version (from the release, or `git describe`) and the board revision printed on the back;
- the host (ESP32, Raspberry Pi, ...), bus speed, cable length and pull-up configuration (JP1/JP2);
- what you expected, what happened, and if possible an I2C capture or a minimal sketch that reproduces it.

## Pull requests

1. Open an issue first for anything larger than a small fix, so we can agree on the approach.
2. Keep changes focused: one topic per pull request.
3. Make sure everything still builds and passes:

   ```sh
   cmake --preset keyboard && cmake --build --preset keyboard
   cmake --preset nucleo   && cmake --build --preset nucleo
   make -C tests
   ```

   If you change the I2C, power or matrix code, please also run the Renode tests
   (see [docs/TESTING.md](docs/TESTING.md)). The CI runs all of them on every pull request.
4. Add or update tests for behaviour you change. Logic in `src/app` must stay hardware-independent so that it
   keeps building for the PC tests and the WebAssembly simulator.
5. Follow the existing style: C11, 4-space indentation, no dynamic memory, no vendor HAL, warnings are errors.
6. Changes to the wire protocol need a matching update of [docs/PROTOCOL.md](docs/PROTOCOL.md) and must stay
   backward compatible within protocol version 1.x.

## Licensing of contributions

By submitting a contribution you agree that it is licensed under the same license as the files you change:
Apache License 2.0 for code (see section 5 of the license), CC BY 4.0 for documentation in `docs/`.

Please sign off your commits (`git commit -s`) to certify the
[Developer Certificate of Origin](https://developercertificate.org/): that you wrote the change or otherwise
have the right to submit it under these licenses.

New source files should start with an SPDX header:

<!-- REUSE-IgnoreStart -->
```c
/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD and contributors
 * SPDX-License-Identifier: Apache-2.0 */
```
<!-- REUSE-IgnoreEnd -->
