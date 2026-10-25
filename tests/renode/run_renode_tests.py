#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 BFU Electronics LTD
# SPDX-License-Identifier: Apache-2.0
"""
Runs the real firmware images in the Renode emulator (STM32G0 platform) and checks:
  1. NUCLEO build: USER button -> scripted typing; UART log must contain the exact phrase,
     INT (PA10) must follow the FIFO state, the UA LED (PA5) must follow the language.
  2. Keyboard build: a column pulled low -> matrix scan -> 4 events with the Ctrl bit,
     INT (PB5) asserted, then Stop mode (SysTick frozen) after the idle timeout.

Usage:  RENODE=/path/to/renode python3 tests/renode/run_renode_tests.py
Build both presets first (cmake --preset keyboard/nucleo && cmake --build ...).
Limits: Renode's stock STM32G0 platform has no I2C master to drive the slave and a different
EXTI block, so I2C traffic and wake-up from Stop are covered by tests/renode_full instead.
"""
import os, re, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RENODE = os.environ.get('RENODE', 'renode')
PLATFORM = os.path.join(os.path.dirname(os.path.realpath(RENODE)), 'platforms', 'cpus', 'stm32g0.repl')
EXPECT_TEXT = 'Привіт, BFU! ʼ₴ Ґ'

failures = []


def check(cond, msg):
    print(('PASS ' if cond else 'FAIL ') + msg)
    if not cond:
        failures.append(msg)


def nm(elf, sym):
    out = subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == sym:
            return int(p[0], 16)
    raise SystemExit(f'symbol {sym} not found in {elf}')


def arm_offsets():
    """Member offsets of app_t as laid out by the ARM compiler (short enums!)."""
    src = ('#include <stddef.h>\n#include "app.h"\n'
           'const unsigned o[3] = { offsetof(app_t,fifo)+offsetof(fifo_t,head), '
           'offsetof(app_t,fifo)+offsetof(fifo_t,count), offsetof(app_t,fifo) };\n')
    with tempfile.TemporaryDirectory() as d:
        c, o = os.path.join(d, 'o.c'), os.path.join(d, 'o.o')
        open(c, 'w').write(src)
        subprocess.run(['arm-none-eabi-gcc', '-mcpu=cortex-m0plus', '-mthumb', '-c', '-O1',
                        '-I', os.path.join(ROOT, 'src', 'app'), c, '-o', o], check=True)
        dump = subprocess.run(['arm-none-eabi-objdump', '-s', '-j', '.rodata', o],
                              capture_output=True, text=True, check=True).stdout
    words = ''.join(l[6:41].replace(' ', '') for l in dump.splitlines() if l.startswith(' 0000'))
    vals = [int.from_bytes(bytes.fromhex(words[i:i + 8]), 'little') for i in range(0, 24, 8)]
    return {'head': vals[0], 'count': vals[1], 'fifo': vals[2]}


def run(script):
    with tempfile.NamedTemporaryFile('w', suffix='.resc', delete=False) as f:
        f.write(script + '\nquit\n')
        path = f.name
    out = subprocess.run([RENODE, '--disable-xwt', '--console', '-e', f'include @{path}'],
                         capture_output=True, text=True, timeout=600).stdout
    os.unlink(path)
    out = re.sub(r'\x1b\[[0-9;]*m', '', out)
    vals = {}
    # the Renode console occasionally drops the first character of a line, so the
    # markers are padded and matched without anchoring to the line start
    for m in re.finditer(r'#+ ([A-Z0-9]+) #+\s*\n.*?(0x[0-9A-Fa-f]+)', out, re.S):
        vals[m.group(1)] = int(m.group(2), 16)
    return vals, out


def mark(name, cmd):
    return f'echo "#### {name} ####"\n{cmd}'


def test_nucleo(off):
    elf = os.path.join(ROOT, 'build', 'nucleo', 'bfu-kb56-nucleo-g071rb.elf')
    app = nm(elf, 'g_app')
    head, count = app + off['head'], app + off['count']
    clear = '\n'.join(f'sysbus WriteByte {hex(a)} 0' for a in (head, head + 1, count))
    uart = tempfile.mktemp(suffix='.txt')
    vals, out = run(f'''mach create "nucleo"
machine LoadPlatformDescription @{PLATFORM}
sysbus LoadELF @{elf}
sysbus.usart2 CreateFileBackend @{uart} true
emulation RunFor "0.05"
{mark('BOOT', 'sysbus ReadDoubleWord 0x50000014')}
sysbus.gpioPortC OnGPIO 13 true
emulation RunFor "0.02"
sysbus.gpioPortC OnGPIO 13 false
emulation RunFor "0.1"
{mark('MIDCOUNT', f'sysbus ReadByte {hex(count)}')}
{mark('MID', 'sysbus ReadDoubleWord 0x50000014')}
{clear}
emulation RunFor "0.3"
{mark('ENDCOUNT', f'sysbus ReadByte {hex(count)}')}
{mark('END', 'sysbus ReadDoubleWord 0x50000014')}
{clear}
emulation RunFor "0.05"
{mark('EMPTY', 'sysbus ReadDoubleWord 0x50000014')}''')
    log = open(uart, encoding='utf-8', errors='replace').read() if os.path.exists(uart) else ''
    text = ''.join(re.findall(r'"([^"]*)"', log))
    print(log)
    check('NUCLEO-G071RB test' in log, 'nucleo: firmware boots and prints banner')
    check(text == EXPECT_TEXT, f'nucleo: typed text == "{EXPECT_TEXT}" (got "{text}")')
    check('ctrl 0x0a' in log, 'nucleo: Enter produces 0x0A')
    check('special 0x20' in log, 'nucleo: language key produces special 0x20')
    b, m, e, z = vals.get('BOOT', -1), vals.get('MID', -1), vals.get('END', -1), vals.get('EMPTY', -1)
    check(b & (1 << 10) and b & (1 << 5), 'nucleo boot: INT released, UA LED on')
    check(vals.get('MIDCOUNT', 0) > 0 and not (m & (1 << 10)), 'nucleo: INT low while events wait')
    check(not (m & (1 << 5)), 'nucleo: LED off while language is EN')
    check(vals.get('ENDCOUNT', 0) > 0 and not (e & (1 << 10)) and (e & (1 << 5)), 'nucleo end: INT low, LED on (UA)')
    check(z & (1 << 10), 'nucleo: INT released after FIFO emptied')


def test_keyboard(off):
    elf = os.path.join(ROOT, 'build', 'keyboard', 'bfu-kb56.elf')
    app, gms = nm(elf, 'g_app'), nm(elf, 'g_ms')
    count, fifo = app + off['count'], app + off['fifo']
    pullups = '\n'.join(f'sysbus.gpioPortA OnGPIO {p} true' for p in (0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 15))
    reads = '\n'.join(mark(f'EV{i}', f'sysbus ReadDoubleWord {hex(fifo + 4 * i)}') for i in range(8))
    vals, out = run(f'''mach create "kb"
machine LoadPlatformDescription @{PLATFORM}
sysbus LoadELF @{elf}
{pullups}
sysbus.gpioPortB OnGPIO 9 true
emulation RunFor "0.02"
{mark('IDLEB', 'sysbus ReadDoubleWord 0x50000414')}
{mark('IDLEC', 'sysbus ReadDoubleWord 0x50000814')}
sysbus.gpioPortA OnGPIO 1 false
emulation RunFor "0.03"
sysbus.gpioPortA OnGPIO 1 true
emulation RunFor "0.005"
{mark('COUNT', f'sysbus ReadByte {hex(count)}')}
{reads}
{mark('PRESSB', 'sysbus ReadDoubleWord 0x50000414')}
emulation RunFor "0.2"
{mark('MS1', f'sysbus ReadDoubleWord {hex(gms)}')}
emulation RunFor "0.2"
{mark('MS2', f'sysbus ReadDoubleWord {hex(gms)}')}
{mark('SLEEPB', 'sysbus ReadDoubleWord 0x50000414')}''')
    ib, ic = vals.get('IDLEB', 0), vals.get('IDLEC', 0)
    check((ib & 0x1F) == 0x1F and ib & (1 << 5), 'keyboard idle: rows high, INT released')
    check(ic & (1 << 6), 'keyboard idle: UA LED (PC6) on')
    check(vals.get('COUNT') == 4, f'keyboard: 4 events for column 1 (got {vals.get("COUNT")})')
    raw = b''.join(vals.get(f'EV{i}', 0).to_bytes(4, 'little') for i in range(8))
    evs = [raw[i:i + 8] for i in range(0, 32, 8)]
    texts = [e[4:8].rstrip(b'\0').decode('utf-8', 'replace') for e in evs]
    check(texts == ['1', 'ц', 'і', 'я'], f'keyboard: texts {texts}')
    check(all(e[0] == 1 and e[2] == 0x02 and e[3] == 0x01 for e in evs), 'keyboard: press, Ctrl bit, UA state')
    check([e[1] for e in evs] == [1, 13, 25, 37], f'keyboard: key codes {[e[1] for e in evs]}')
    check(not (vals.get('PRESSB', 0xFF) & (1 << 5)), 'keyboard: INT low with events pending')
    check(vals.get('MS1') == vals.get('MS2'), 'keyboard: entered Stop (SysTick frozen) when idle')
    check((vals.get('SLEEPB', 0xFF) & 0x1F) == 0, 'keyboard: all rows low while in Stop (wake-up armed)')


if __name__ == '__main__':
    off = arm_offsets()
    test_nucleo(off)
    test_keyboard(off)
    print(f'\n{len(failures)} failure(s)')
    sys.exit(1 if failures else 0)
